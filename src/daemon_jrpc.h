#ifndef DAEMON_JRPC_H
#define DAEMON_JRPC_H

#include "jrpc.h"

/**
 * Handle single JSON-RPC request
 * @param client_fd Client socket file descriptor
 * @param req JSON-RPC request object
 * @return 0 on success, -1 on failure
 */
int handle_jsonrpc_request(int client_fd, const jsonrpc_req_t *req);

/**
 * Daemon JSON-RPC main loop
 * @param server_fd Listening socket file descriptor
 */
void daemon_jsonrpc_loop(int server_fd);

#endif // DAEMON_JRPC_H
