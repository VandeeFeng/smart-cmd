#ifndef JRPC_H
#define JRPC_H

#include <json-c/json.h>
#include <stdint.h>

// JSON-RPC 2.0 method definitions
typedef enum {
    RPC_METHOD_PING = 0,           // Health check
    RPC_METHOD_COMPLETE,           // Get completion suggestions
    RPC_METHOD_PUSH_HISTORY,       // Push command history
    RPC_METHOD_GET_HISTORY,        // Get command history
    RPC_METHOD_CLEAR_HISTORY,      // Clear history
    RPC_METHOD_GET_STATUS,         // Get daemon status
    RPC_METHOD_SHUTDOWN            // Shutdown daemon
} rpc_method_t;

// JSON-RPC error codes
typedef enum {
    RPC_ERROR_PARSE_ERROR = -32700,
    RPC_ERROR_INVALID_REQUEST = -32600,
    RPC_ERROR_METHOD_NOT_FOUND = -32601,
    RPC_ERROR_INVALID_PARAMS = -32602,
    RPC_ERROR_INTERNAL_ERROR = -32603,
    RPC_ERROR_SERVER_ERROR = -32000
} rpc_error_code_t;

// JSON-RPC request structure
typedef struct {
    json_object *request;   // Complete JSON-RPC request object
    int id;                 // Request ID
    char *method_str;       // Method name string
} jsonrpc_req_t;

// JSON-RPC response structure
typedef struct {
    json_object *response;  // Complete JSON-RPC response object
    json_object *result;    // result field
    json_object *error;     // error field
    int id;                 // Response ID
    int has_error;          // Whether response contains error
} jsonrpc_resp_t;

// Core request/response API
/**
 * Build JSON-RPC request
 * @param method RPC method
 * @param params Parameter object (will take ownership)
 * @param id Request ID (use 0 for notification, no response)
 * @return Newly allocated request object, must be freed with rpc_free_request
 */
jsonrpc_req_t* rpc_build_request(rpc_method_t method, json_object *params, int id);

/**
 * Parse JSON-RPC request from string
 * @param json_str JSON string
 * @return Newly allocated request object, must be freed with rpc_free_request
 */
jsonrpc_req_t* rpc_parse_request(const char *json_str);

/**
 * Build success response
 * @param result Result object (will take ownership)
 * @param id Request ID
 * @return Newly allocated response object, must be freed with rpc_free_response
 */
jsonrpc_resp_t* rpc_build_success_response(json_object *result, int id);

/**
 * Build error response
 * @param code Error code
 * @param message Error message
 * @param id Request ID (can be NULL)
 * @return Newly allocated response object, must be freed with rpc_free_response
 */
jsonrpc_resp_t* rpc_build_error_response(int code, const char *message, int id);

/**
 * Parse JSON-RPC response from string
 * @param json_str JSON string
 * @return Newly allocated response object, must be freed with rpc_free_response
 */
jsonrpc_resp_t* rpc_parse_response(const char *json_str);

/**
 * Free request object
 */
void rpc_free_request(jsonrpc_req_t *req);

/**
 * Free response object
 */
void rpc_free_response(jsonrpc_resp_t *resp);

// Socket transport API (Unix Socket based)
/**
 * Send JSON-RPC request via Unix socket
 * @param socket_fd Socket file descriptor
 * @param req Request object
 * @return 0 on success, -1 on failure
 */
int rpc_send_request(int socket_fd, const jsonrpc_req_t *req);

/**
 * Receive JSON-RPC response via Unix socket
 * @param socket_fd Socket file descriptor
 * @param resp_out Output response object pointer
 * @return 0 on success, -1 on failure
 */
int rpc_recv_response(int socket_fd, jsonrpc_resp_t **resp_out);

/**
 * Send JSON-RPC response via Unix socket
 * @param socket_fd Socket file descriptor
 * @param resp Response object
 * @return 0 on success, -1 on failure
 */
int rpc_send_response(int socket_fd, const jsonrpc_resp_t *resp);

/**
 * Receive JSON-RPC request via Unix socket
 * @param socket_fd Socket file descriptor
 * @param req_out Output request object pointer
 * @return 0 on success, -1 on failure
 */
int rpc_recv_request(int socket_fd, jsonrpc_req_t **req_out);

// Utility functions
/**
 * Convert RPC method enum to string
 */
const char* rpc_method_to_string(rpc_method_t method);

/**
 * Convert string to RPC method enum
 */
rpc_method_t rpc_string_to_method(const char *method_str);

/**
 * Extract JSON-RPC ID
 */
int rpc_get_id(const json_object *json_obj);

/**
 * Validate JSON-RPC object basic format
 */
int rpc_validate_jsonrpc(const json_object *json_obj);

#endif // JRPC_H
