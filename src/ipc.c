#define _GNU_SOURCE
#include "smart_cmd.h"
#include "jrpc.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define IPC_TIMEOUT_MS 5000
#define IPC_BACKLOG 5

// Socket setup and helper functions
static void set_socket_timeout(int fd) {
    struct timeval timeout;
    timeout.tv_sec = IPC_TIMEOUT_MS / 1000;
    timeout.tv_usec = (IPC_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int create_ipc_socket(const char *socket_path) {
    if (!socket_path) return -1;

    unlink(socket_path);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(server_fd);
        perror("fcntl");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    chmod(socket_path, 0600);

    if (listen(server_fd, IPC_BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        unlink(socket_path);
        return -1;
    }

    return server_fd;
}

int accept_ipc_connection(int server_fd) {
    if (server_fd == -1) return -1;

    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        perror("accept");
        return -1;
    }

    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
        if (cred.uid != getuid()) {
            fprintf(stderr, "ERROR: accept_ipc_connection: Rejecting connection from different user (UID: %d)\n", cred.uid);
            close(client_fd);
            return -1;
        }
    }

    set_socket_timeout(client_fd);

    return client_fd;
}

void cleanup_ipc_socket(const char *socket_path) {
    if (socket_path) {
        unlink(socket_path);
    }
}

// Client connection functions
int connect_to_daemon(const char *socket_path) {
    if (!socket_path) return -1;

    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(client_fd);
        return -1;
    }

    set_socket_timeout(client_fd);

    return client_fd;
}

// High-level JSON-RPC wrapper functions

int ping_daemon(const char *socket_path) {
    if (!socket_path) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) return -1;

    jsonrpc_req_t *req = rpc_build_request(RPC_METHOD_PING, NULL, 1);
    if (!req) {
        close(client_fd);
        return -1;
    }

    if (rpc_send_request(client_fd, req) != 0) {
        rpc_free_request(req);
        close(client_fd);
        return -1;
    }

    jsonrpc_resp_t *resp = NULL;
    int result = -1;

    if (rpc_recv_response(client_fd, &resp) == 0 && !resp->has_error) {
        json_object *result_obj;
        if (json_object_object_get_ex(resp->result, "result", &result_obj)) {
            const char *result_str = json_object_get_string(result_obj);
            if (result_str && strcmp(result_str, "pong") == 0) {
                result = 0;
            }
        }
    }

    rpc_free_response(resp);
    rpc_free_request(req);
    close(client_fd);

    return result;
}

int request_completion(const char *socket_path, const char *input, suggestion_t *suggestion) {
    if (!socket_path || !input || !suggestion) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) return -1;

    json_object *params = json_object_new_object();
    json_object_object_add(params, "input", json_object_new_string(input));

    jsonrpc_req_t *req = rpc_build_request(RPC_METHOD_COMPLETE, params, 1);
    if (!req) {
        close(client_fd);
        return -1;
    }

    if (rpc_send_request(client_fd, req) != 0) {
        rpc_free_request(req);
        close(client_fd);
        return -1;
    }

    jsonrpc_resp_t *resp = NULL;
    int result = -1;

    if (rpc_recv_response(client_fd, &resp) == 0) {
        if (resp->has_error) {
            json_object *msg_obj;
            if (json_object_object_get_ex(resp->error, "message", &msg_obj)) {
                fprintf(stderr, "RPC Error: %s\n", json_object_get_string(msg_obj));
            }
        } else {
            json_object *suggestion_obj, *type_obj;
            if (json_object_object_get_ex(resp->result, "suggestion", &suggestion_obj) &&
                json_object_object_get_ex(resp->result, "type", &type_obj)) {

                strncpy(suggestion->suggestion,
                        json_object_get_string(suggestion_obj),
                        sizeof(suggestion->suggestion) - 1);
                suggestion->type = json_object_get_int(type_obj);
                suggestion->visible = 1;
                result = 0;
            }
        }
    }

    rpc_free_response(resp);
    rpc_free_request(req);
    close(client_fd);

    return result;
}

int push_history_to_daemon(const char *socket_path, const char *command) {
    if (!socket_path || !command) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) return -1;

    json_object *params = json_object_new_object();
    json_object_object_add(params, "command", json_object_new_string(command));

    jsonrpc_req_t *req = rpc_build_request(RPC_METHOD_PUSH_HISTORY, params, 0);
    if (!req) {
        close(client_fd);
        return -1;
    }

    int result = rpc_send_request(client_fd, req);

    rpc_free_request(req);
    close(client_fd);

    return result;
}

int get_daemon_status(const char *socket_path, char *status_buf, size_t buf_size) {
    if (!socket_path || !status_buf) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) return -1;

    jsonrpc_req_t *req = rpc_build_request(RPC_METHOD_GET_STATUS, NULL, 1);
    if (!req) {
        close(client_fd);
        return -1;
    }

    if (rpc_send_request(client_fd, req) != 0) {
        rpc_free_request(req);
        close(client_fd);
        return -1;
    }

    jsonrpc_resp_t *resp = NULL;
    int result = -1;

    if (rpc_recv_response(client_fd, &resp) == 0 && !resp->has_error) {
        const char *status_str = json_object_to_json_string_ext(resp->result, JSON_C_TO_STRING_PRETTY);
        if (status_str) {
            strncpy(status_buf, status_str, buf_size - 1);
            status_buf[buf_size - 1] = '\0';
            result = 0;
        }
    }

    rpc_free_response(resp);
    rpc_free_request(req);
    close(client_fd);

    return result;
}

int shutdown_daemon(const char *socket_path) {
    if (!socket_path) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) return -1;

    jsonrpc_req_t *req = rpc_build_request(RPC_METHOD_SHUTDOWN, NULL, 1);
    if (!req) {
        close(client_fd);
        return -1;
    }

    if (rpc_send_request(client_fd, req) != 0) {
        rpc_free_request(req);
        close(client_fd);
        return -1;
    }

    jsonrpc_resp_t *resp = NULL;
    int result = -1;

    if (rpc_recv_response(client_fd, &resp) == 0 && !resp->has_error) {
        result = 0;
    }

    rpc_free_response(resp);
    rpc_free_request(req);
    close(client_fd);

    return result;
}
