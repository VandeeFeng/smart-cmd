#define _GNU_SOURCE
#include "jrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

// Method name mapping table
static const char* method_names[] = {
    "ping",
    "complete",
    "push_history",
    "get_history",
    "clear_history",
    "get_status",
    "shutdown"
};

static const int NUM_METHODS = sizeof(method_names) / sizeof(method_names[0]);

// Utility functions
const char* rpc_method_to_string(rpc_method_t method) {
    if (method >= 0 && method < NUM_METHODS) {
        return method_names[method];
    }
    return "unknown";
}

rpc_method_t rpc_string_to_method(const char *method_str) {
    if (!method_str) return -1;

    for (int i = 0; i < NUM_METHODS; i++) {
        if (strcmp(method_str, method_names[i]) == 0) {
            return (rpc_method_t)i;
        }
    }
    return -1;
}

int rpc_get_id(const json_object *json_obj) {
    if (!json_obj) return 0;

    json_object *id_obj;
    if (json_object_object_get_ex(json_obj, "id", &id_obj)) {
        return json_object_get_int(id_obj);
    }
    return 0;
}

int rpc_validate_jsonrpc(const json_object *json_obj) {
    if (!json_obj) return -1;

    json_object *version_obj;
    if (!json_object_object_get_ex(json_obj, "jsonrpc", &version_obj)) {
        return -1;
    }

    const char *version = json_object_get_string(version_obj);
    if (!version || strcmp(version, "2.0") != 0) {
        return -1;
    }

    return 0;
}

// Request building and parsing
jsonrpc_req_t* rpc_build_request(rpc_method_t method, json_object *params, int id) {
    jsonrpc_req_t *req = calloc(1, sizeof(jsonrpc_req_t));
    if (!req) {
        fprintf(stderr, "ERROR: rpc_build_request: Failed to allocate memory\n");
        return NULL;
    }

    req->request = json_object_new_object();
    if (!req->request) {
        free(req);
        fprintf(stderr, "ERROR: rpc_build_request: Failed to create JSON object\n");
        return NULL;
    }

    // JSON-RPC 2.0 standard fields
    json_object_object_add(req->request, "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(req->request, "method", json_object_new_string(rpc_method_to_string(method)));

    // Add parameters (create empty object if none)
    if (params) {
        json_object_object_add(req->request, "params", params);
    } else {
        json_object_object_add(req->request, "params", json_object_new_object());
    }

    // Add ID
    req->id = id;
    json_object_object_add(req->request, "id", json_object_new_int(id));

    // Cache method name string
    req->method_str = strdup(rpc_method_to_string(method));

    return req;
}

jsonrpc_req_t* rpc_parse_request(const char *json_str) {
    if (!json_str) {
        fprintf(stderr, "ERROR: rpc_parse_request: NULL input\n");
        return NULL;
    }

    jsonrpc_req_t *req = calloc(1, sizeof(jsonrpc_req_t));
    if (!req) {
        fprintf(stderr, "ERROR: rpc_parse_request: Failed to allocate memory\n");
        return NULL;
    }

    req->request = json_tokener_parse(json_str);
    if (!req->request) {
        fprintf(stderr, "ERROR: rpc_parse_request: Failed to parse JSON\n");
        free(req);
        return NULL;
    }

    // Validate JSON-RPC version
    if (rpc_validate_jsonrpc(req->request) != 0) {
        fprintf(stderr, "ERROR: rpc_parse_request: Invalid JSON-RPC version\n");
        json_object_put(req->request);
        free(req);
        return NULL;
    }

    // Extract ID
    req->id = rpc_get_id(req->request);

    // Extract method name
    json_object *method_obj;
    if (json_object_object_get_ex(req->request, "method", &method_obj)) {
        req->method_str = strdup(json_object_get_string(method_obj));
    }

    return req;
}

void rpc_free_request(jsonrpc_req_t *req) {
    if (req) {
        if (req->request) json_object_put(req->request);
        if (req->method_str) free(req->method_str);
        free(req);
    }
}

// Response building and parsing
jsonrpc_resp_t* rpc_build_success_response(json_object *result, int id) {
    jsonrpc_resp_t *resp = calloc(1, sizeof(jsonrpc_resp_t));
    if (!resp) {
        fprintf(stderr, "ERROR: rpc_build_success_response: Failed to allocate memory\n");
        return NULL;
    }

    resp->response = json_object_new_object();
    if (!resp->response) {
        free(resp);
        fprintf(stderr, "ERROR: rpc_build_success_response: Failed to create JSON object\n");
        return NULL;
    }

    json_object_object_add(resp->response, "jsonrpc", json_object_new_string("2.0"));

    // Take ownership of result object
    if (result) {
        resp->result = result;
        json_object_object_add(resp->response, "result", result);
    } else {
        json_object_object_add(resp->response, "result", json_object_new_object());
    }

    resp->id = id;
    json_object_object_add(resp->response, "id", json_object_new_int(id));

    resp->has_error = 0;

    return resp;
}

jsonrpc_resp_t* rpc_build_error_response(int code, const char *message, int id) {
    jsonrpc_resp_t *resp = calloc(1, sizeof(jsonrpc_resp_t));
    if (!resp) {
        fprintf(stderr, "ERROR: rpc_build_error_response: Failed to allocate memory\n");
        return NULL;
    }

    resp->response = json_object_new_object();
    if (!resp->response) {
        free(resp);
        fprintf(stderr, "ERROR: rpc_build_error_response: Failed to create JSON object\n");
        return NULL;
    }

    json_object_object_add(resp->response, "jsonrpc", json_object_new_string("2.0"));

    // Build error object
    resp->error = json_object_new_object();
    json_object_object_add(resp->error, "code", json_object_new_int(code));
    json_object_object_add(resp->error, "message", json_object_new_string(message ? message : "Unknown error"));
    json_object_object_add(resp->response, "error", resp->error);

    resp->id = id;
    if (id >= 0) {
        json_object_object_add(resp->response, "id", json_object_new_int(id));
    } else {
        json_object_object_add(resp->response, "id", NULL);
    }

    resp->has_error = 1;

    return resp;
}

jsonrpc_resp_t* rpc_parse_response(const char *json_str) {
    if (!json_str) {
        fprintf(stderr, "ERROR: rpc_parse_response: NULL input\n");
        return NULL;
    }

    jsonrpc_resp_t *resp = calloc(1, sizeof(jsonrpc_resp_t));
    if (!resp) {
        fprintf(stderr, "ERROR: rpc_parse_response: Failed to allocate memory\n");
        return NULL;
    }

    resp->response = json_tokener_parse(json_str);
    if (!resp->response) {
        fprintf(stderr, "ERROR: rpc_parse_response: Failed to parse JSON\n");
        free(resp);
        return NULL;
    }

    // Validate JSON-RPC version
    if (rpc_validate_jsonrpc(resp->response) != 0) {
        fprintf(stderr, "ERROR: rpc_parse_response: Invalid JSON-RPC version\n");
        json_object_put(resp->response);
        free(resp);
        return NULL;
    }

    // Extract ID
    resp->id = rpc_get_id(resp->response);

    // Check if has error
    json_object *error_obj;
    if (json_object_object_get_ex(resp->response, "error", &error_obj)) {
        resp->error = error_obj;
        resp->has_error = 1;
    } else {
        // Extract result
        json_object *result_obj;
        if (json_object_object_get_ex(resp->response, "result", &result_obj)) {
            resp->result = result_obj;
        }
    }

    return resp;
}

void rpc_free_response(jsonrpc_resp_t *resp) {
    if (resp) {
        // Note: result and error are children of response, no need to put separately
        if (resp->response) json_object_put(resp->response);
        free(resp);
    }
}

// Socket transport implementation
static int send_json_message(int socket_fd, const char *json_str) {
    if (!json_str) return -1;

    size_t len = strlen(json_str);
    if (len == 0 || len > 65536) {
        fprintf(stderr, "ERROR: send_json_message: Invalid message size: %zu\n", len);
        return -1;
    }

    // Use newline as message delimiter
    char *message = NULL;
    int result = -1;

    if (asprintf(&message, "%s\n", json_str) < 0) {
        fprintf(stderr, "ERROR: send_json_message: Failed to allocate message buffer\n");
        return -1;
    }

    ssize_t sent = send(socket_fd, message, strlen(message), MSG_NOSIGNAL);
    if (sent != (ssize_t)strlen(message)) {
        perror("send");
    } else {
        result = 0;
    }

    free(message);
    return result;
}

int rpc_send_request(int socket_fd, const jsonrpc_req_t *req) {
    if (!req || !req->request) {
        fprintf(stderr, "ERROR: rpc_send_request: Invalid request\n");
        return -1;
    }

    const char *json_str = json_object_to_json_string_ext(req->request, JSON_C_TO_STRING_PLAIN);
    if (!json_str) {
        fprintf(stderr, "ERROR: rpc_send_request: Failed to serialize JSON\n");
        return -1;
    }

    return send_json_message(socket_fd, json_str);
}

int rpc_recv_request(int socket_fd, jsonrpc_req_t **req_out) {
    if (!req_out) return -1;

    char buffer[65536];
    memset(buffer, 0, sizeof(buffer));

    // Read until newline
    size_t total = 0;
    while (total < sizeof(buffer) - 1) {
        ssize_t n = recv(socket_fd, buffer + total, 1, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }

        if (buffer[total] == '\n') {
            break;
        }

        total++;
    }

    if (total == 0) {
        return -1;
    }

    buffer[total] = '\0';

    jsonrpc_req_t *req = rpc_parse_request(buffer);
    if (!req) {
        return -1;
    }

    *req_out = req;
    return 0;
}

int rpc_send_response(int socket_fd, const jsonrpc_resp_t *resp) {
    if (!resp || !resp->response) {
        fprintf(stderr, "ERROR: rpc_send_response: Invalid response\n");
        return -1;
    }

    const char *json_str = json_object_to_json_string_ext(resp->response, JSON_C_TO_STRING_PLAIN);
    if (!json_str) {
        fprintf(stderr, "ERROR: rpc_send_response: Failed to serialize JSON\n");
        return -1;
    }

    return send_json_message(socket_fd, json_str);
}

int rpc_recv_response(int socket_fd, jsonrpc_resp_t **resp_out) {
    if (!resp_out) return -1;

    char buffer[65536];
    memset(buffer, 0, sizeof(buffer));

    // Read until newline
    size_t total = 0;
    while (total < sizeof(buffer) - 1) {
        ssize_t n = recv(socket_fd, buffer + total, 1, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }

        if (buffer[total] == '\n') {
            break;
        }

        total++;
    }

    if (total == 0) {
        return -1;
    }

    buffer[total] = '\0';

    jsonrpc_resp_t *resp = rpc_parse_response(buffer);
    if (!resp) {
        return -1;
    }

    *resp_out = resp;
    return 0;
}
