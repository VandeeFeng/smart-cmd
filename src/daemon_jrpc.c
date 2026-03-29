#define _GNU_SOURCE
#include "smart_cmd.h"
#include "jrpc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define POLL_INTERVAL_US 100000  // 100ms

// Global daemon session state references
extern daemon_session_t g_daemon_info;
extern daemon_pty_t g_daemon_pty;
extern command_history_manager_t g_command_history;
extern volatile sig_atomic_t g_running;

// Forward declarations (functions from daemon_history.c)
extern int add_command_to_history(command_history_manager_t *history, const char *command);
extern void cleanup_command_history(command_history_manager_t *manager);

// Helper: send error response
static int send_error_response(int client_fd, int code, const char *msg, int request_id) {
    jsonrpc_resp_t *resp = rpc_build_error_response(code, msg, request_id);
    if (!resp) return -1;
    int ret = rpc_send_response(client_fd, resp);
    rpc_free_response(resp);
    return ret;
}

// Helper: send success response with json_object result
static int send_success_response(int client_fd, json_object *result, int request_id) {
    if (!result) return -1;
    jsonrpc_resp_t *resp = rpc_build_success_response(result, request_id);
    if (!resp) {
        json_object_put(result);
        return -1;
    }
    int ret = rpc_send_response(client_fd, resp);
    rpc_free_response(resp);
    return ret;
}

// Helper: extract string parameter from json_object
static const char *extract_string_param(json_object *params, const char *key) {
    json_object *obj;
    if (!json_object_object_get_ex(params, key, &obj)) return NULL;
    return json_object_get_string(obj);
}

// JSON-RPC method handlers
static int handle_ping(int client_fd, int request_id) {
    json_object *result = json_object_new_string("pong");
    return send_success_response(client_fd, result, request_id);
}

// Collect session context from daemon PTY buffer
static void collect_session_context(session_context_t *ctx) {
    if (!g_daemon_pty.active) return;

    size_t copy_len = sizeof(ctx->terminal_buffer) - 1;
    size_t buffer_len = strlen(g_daemon_pty.buffer);
    if (buffer_len < copy_len) copy_len = buffer_len;

    memcpy(ctx->terminal_buffer, g_daemon_pty.buffer, copy_len);
    ctx->terminal_buffer[copy_len] = '\0';
    ctx->command_count = g_command_history.count;
}

/**
 * Handle complete request - Get command completion suggestions
 */
static int handle_complete(int client_fd, json_object *params, int request_id) {
    if (!params) {
        return send_error_response(client_fd, RPC_ERROR_INVALID_PARAMS,
                                   "Missing params", request_id);
    }

    const char *input = extract_string_param(params, "input");
    if (!input || strlen(input) == 0) {
        return send_error_response(client_fd, RPC_ERROR_INVALID_PARAMS,
                                   "Invalid or missing 'input' parameter", request_id);
    }

    config_t config;
    if (load_config(&config) != 0) {
        return send_error_response(client_fd, RPC_ERROR_INTERNAL_ERROR,
                                   "Failed to load configuration", request_id);
    }

    session_context_t ctx = {0};
    collect_session_context(&ctx);
    collect_context(&ctx);

    suggestion_t suggestion = {0};
    if (send_to_llm(input, &ctx, &config, &suggestion) != 0) {
        return send_error_response(client_fd, RPC_ERROR_INTERNAL_ERROR,
                                   "Failed to get suggestion from LLM", request_id);
    }

    json_object *result = json_object_new_object();
    json_object_object_add(result, "type", json_object_new_int(suggestion.type));
    json_object_object_add(result, "suggestion", json_object_new_string(suggestion.suggestion));

    return send_success_response(client_fd, result, request_id);
}

/**
 * Handle push_history request - Push command history
 */
static int handle_push_history(int client_fd, json_object *params, int request_id) {
    // Notification mode: return directly on error
    if (!params) return (request_id == 0) ? 0 :
        send_error_response(client_fd, RPC_ERROR_INVALID_PARAMS, "Missing params", request_id);

    const char *command = extract_string_param(params, "command");
    if (!command || strlen(command) == 0) {
        return (request_id == 0) ? 0 :
            send_error_response(client_fd, RPC_ERROR_INVALID_PARAMS,
                               "Invalid or missing 'command' parameter", request_id);
    }

    add_command_to_history(&g_command_history, command);

    if (request_id == 0) return 0;  // Notification mode

    json_object *result = json_object_new_boolean(1);
    return send_success_response(client_fd, result, request_id);
}

/**
 * Handle get_history request - Get command history
 */
static int handle_get_history(int client_fd, json_object *params, int request_id) {
    int count = 10;  // Default: return last 10 commands

    if (params) {
        json_object *count_obj;
        if (json_object_object_get_ex(params, "count", &count_obj)) {
            count = json_object_get_int(count_obj);
            if (count < 1) count = 1;
            if (count > MAX_HISTORY_COMMANDS) count = MAX_HISTORY_COMMANDS;
        }
    }

    json_object *history_array = json_object_new_array();
    int start = (g_command_history.count > count) ? (g_command_history.count - count) : 0;

    for (int i = start; i < g_command_history.count; i++) {
        json_object *entry = json_object_new_object();
        json_object_object_add(entry, "command",
                               json_object_new_string(g_command_history.commands[i].command));
        json_object_object_add(entry, "timestamp",
                               json_object_new_int64((int64_t)g_command_history.commands[i].timestamp));
        json_object_array_add(history_array, entry);
    }

    json_object *result = json_object_new_object();
    json_object_object_add(result, "total", json_object_new_int(g_command_history.count));
    json_object_object_add(result, "history", history_array);

    return send_success_response(client_fd, result, request_id);
}

/**
 * Handle clear_history request - Clear command history
 */
static int handle_clear_history(int client_fd, int request_id) {
    cleanup_command_history(&g_command_history);
    g_command_history.count = 0;

    json_object *result = json_object_new_string("History cleared");
    return send_success_response(client_fd, result, request_id);
}

/**
 * Handle get_status request - Get daemon status
 */
static int handle_get_status(int client_fd, int request_id) {
    json_object *result = json_object_new_object();

    json_object_object_add(result, "daemon_pid", json_object_new_int(getpid()));
    json_object_object_add(result, "session_id", json_object_new_string(g_daemon_info.paths.session_id));
    json_object_object_add(result, "socket_path", json_object_new_string(g_daemon_info.paths.socket_path));

    time_t uptime = time(NULL) - g_daemon_info.start_time;
    json_object_object_add(result, "uptime_seconds", json_object_new_int64((int64_t)uptime));

    json_object *pty_obj = json_object_new_object();
    json_object_object_add(pty_obj, "active", json_object_new_boolean(g_daemon_pty.active));
    json_object_object_add(pty_obj, "buffer_size", json_object_new_int(g_daemon_pty.buffer_pos));
    json_object_object_add(result, "pty", pty_obj);

    json_object *history_obj = json_object_new_object();
    json_object_object_add(history_obj, "total_commands", json_object_new_int(g_command_history.count));
    json_object_object_add(history_obj, "max_commands", json_object_new_int(MAX_HISTORY_COMMANDS));
    json_object_object_add(result, "history", history_obj);

    config_t config;
    if (load_config(&config) == 0) {
        json_object *config_obj = json_object_new_object();
        json_object_object_add(config_obj, "provider", json_object_new_string(config.llm.provider));
        json_object_object_add(config_obj, "model", json_object_new_string(config.llm.model));
        json_object_object_add(result, "config", config_obj);
    }

    return send_success_response(client_fd, result, request_id);
}

/**
 * Handle shutdown request - Shutdown daemon
 */
static int handle_shutdown(int client_fd, int request_id) {
    json_object *result = json_object_new_string("Daemon shutting down");
    int ret = send_success_response(client_fd, result, request_id);

    g_running = 0;  // Trigger graceful shutdown
    return ret;
}

// Method handler function type
typedef int (*method_handler_t)(int client_fd, json_object *params, int request_id);

// Method registry entry
typedef struct {
    const char *name;
    method_handler_t handler;
} method_entry_t;

// Handlers with params signature
static int handle_ping_wrap(int client_fd, json_object *params, int request_id) {
    (void)params;
    return handle_ping(client_fd, request_id);
}

static int handle_clear_history_wrap(int client_fd, json_object *params, int request_id) {
    (void)params;
    return handle_clear_history(client_fd, request_id);
}

static int handle_get_status_wrap(int client_fd, json_object *params, int request_id) {
    (void)params;
    return handle_get_status(client_fd, request_id);
}

static int handle_shutdown_wrap(int client_fd, json_object *params, int request_id) {
    (void)params;
    return handle_shutdown(client_fd, request_id);
}

// Method registry
static const method_entry_t method_table[] = {
    {"ping",          handle_ping_wrap},
    {"complete",      handle_complete},
    {"push_history",  handle_push_history},
    {"get_history",   handle_get_history},
    {"clear_history", handle_clear_history_wrap},
    {"get_status",    handle_get_status_wrap},
    {"shutdown",      handle_shutdown_wrap},
    {NULL,            NULL}  // Sentinel
};

// JSON-RPC request router
int handle_jsonrpc_request(int client_fd, const jsonrpc_req_t *req) {
    if (!req || !req->method_str) {
        return send_error_response(client_fd, RPC_ERROR_INVALID_REQUEST,
                                   "Invalid JSON-RPC request", 0);
    }

    json_object *params = NULL;
    json_object_object_get_ex(req->request, "params", &params);

    for (const method_entry_t *entry = method_table; entry->name; entry++) {
        if (strcmp(req->method_str, entry->name) == 0) {
            return entry->handler(client_fd, params, req->id);
        }
    }

    return send_error_response(client_fd, RPC_ERROR_METHOD_NOT_FOUND,
                               "Method not found", req->id);
}

/**
 * Daemon JSON-RPC main loop
 * Handle client connections and requests
 */
void daemon_jsonrpc_loop(int server_fd) {
    while (g_running) {
        int client_fd = accept_ipc_connection(server_fd);
        if (client_fd <= 0) {
            if (client_fd == 0) {
                usleep(POLL_INTERVAL_US);
                continue;
            }
            break;
        }

        jsonrpc_req_t *req = NULL;
        if (rpc_recv_request(client_fd, &req) != 0) {
            close(client_fd);
            continue;
        }

        handle_jsonrpc_request(client_fd, req);

        rpc_free_request(req);
        close(client_fd);
    }
}
