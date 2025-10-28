#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define MAX_IPC_MESSAGE_SIZE 4096
#define IPC_TIMEOUT_MS 5000

// Message types
typedef enum {
    MSG_TYPE_PING = 1,
    MSG_TYPE_SUGGESTION = 2,
    MSG_TYPE_CONTEXT = 3,
    MSG_TYPE_COMMAND = 4,
    MSG_TYPE_RESPONSE = 5,
    MSG_TYPE_ERROR = 6
} ipc_message_type_t;

// IPC message header
typedef struct {
    uint32_t magic;      // Magic number for validation
    uint32_t version;    // Protocol version
    uint32_t type;       // Message type
    uint32_t length;     // Message length
    uint32_t timestamp;  // Unix timestamp
    char session_id[32]; // Session identifier
} ipc_header_t;

#define IPC_MAGIC 0x534D5443  // "SMTC"
#define IPC_VERSION 1

int validate_ipc_message(const char *message) {
    if (!message) return -1;

    // Basic validation: check for null bytes and reasonable length
    size_t len = strlen(message);
    if (len == 0 || len > MAX_IPC_MESSAGE_SIZE - sizeof(ipc_header_t)) {
        return -1;
    }

    // Check for potential injection attempts
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)message[i] < 32 && message[i] != '\t' && message[i] != '\n') {
            return -1; // Control characters not allowed
        }
    }

    // Check for suspicious patterns
    if (strstr(message, "..") || strstr(message, "~") || strstr(message, "$(")) {
        return -1; // Potential path traversal or command injection
    }

    return 0;
}

int create_ipc_socket(const char *socket_path) {
    if (!socket_path) return -1;

    // Remove existing socket file if any
    unlink(socket_path);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    // Set socket to non-blocking
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

    // Set strict permissions on socket file
    chmod(socket_path, 0600);

    if (listen(server_fd, 5) == -1) {
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

    // Verify peer credentials (Unix domain socket specific)
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
        // Verify that the peer is running as the same user
        if (cred.uid != getuid()) {
            fprintf(stderr, "ERROR: accept_client_connection: Rejecting connection from different user (UID: %d)\n", cred.uid);
            close(client_fd);
            return -1;
        }
    }

    // Set timeout for client operations
    struct timeval timeout;
    timeout.tv_sec = IPC_TIMEOUT_MS / 1000;
    timeout.tv_usec = (IPC_TIMEOUT_MS % 1000) * 1000;

    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    return client_fd;
}

int send_ipc_message(int fd, const char *message) {
    if (fd == -1 || !message) return -1;

    int err;
    if ((err = validate_ipc_message(message)) != 0) {
        fprintf(stderr, "ERROR: send_ipc_message: Invalid IPC message rejected\n");
        return -1;
    }

    size_t msg_len = strlen(message);
    if (msg_len > MAX_IPC_MESSAGE_SIZE - sizeof(ipc_header_t)) {
        fprintf(stderr, "ERROR: send_ipc_message: Message too long\n");
        return -1;
    }

    // Prepare header
    ipc_header_t header;
    header.magic = IPC_MAGIC;
    header.version = IPC_VERSION;
    header.type = MSG_TYPE_SUGGESTION;
    header.length = msg_len;
    header.timestamp = time(NULL);
    memset(header.session_id, 0, sizeof(header.session_id));

    // Send header
    ssize_t sent = send(fd, &header, sizeof(header), MSG_NOSIGNAL);
    if (sent != sizeof(header)) {
        perror("send header");
        return -1;
    }

    // Send message
    sent = send(fd, message, msg_len, MSG_NOSIGNAL);
    if (sent != (ssize_t)msg_len) {
        perror("send message");
        return -1;
    }

    return 0;
}

int receive_ipc_message(int fd, char *buffer, size_t buffer_size) {
    if (fd == -1 || !buffer || buffer_size < sizeof(ipc_header_t) + 1) {
        return -1;
    }

    // Receive header
    ipc_header_t header;
    ssize_t received = recv(fd, &header, sizeof(header), 0);
    if (received != sizeof(header)) {
        if (received == 0) {
            return 0;
        }
        perror("recv header");
        return -1;
    }

    // Validate header
    if (header.magic != IPC_MAGIC || header.version != IPC_VERSION) {
        fprintf(stderr, "ERROR: receive_ipc_message: Invalid IPC header\n");
        return -1;
    }

    if (header.length > MAX_IPC_MESSAGE_SIZE - sizeof(ipc_header_t)) {
        fprintf(stderr, "ERROR: receive_ipc_message: Message too long: %u bytes\n", header.length);
        return -1;
    }

    if (header.length > buffer_size - 1) {
        fprintf(stderr, "ERROR: receive_ipc_message: Buffer too small for message\n");
        return -1;
    }

    // Receive message body
    received = recv(fd, buffer, header.length, 0);
    if (received != (ssize_t)header.length) {
        if (received == 0) {
            return 0;
        }
        perror("recv message");
        return -1;
    }

    buffer[received] = '\0';

    // Validate received message
    int err;
    if ((err = validate_ipc_message(buffer)) != 0) {
        fprintf(stderr, "ERROR: receive_ipc_message: Received invalid IPC message\n");
        return -1;
    }

    return received;
}

void cleanup_ipc_socket(const char *socket_path) {
    if (socket_path) {
        unlink(socket_path);
    }
}

// Client-side functions for connecting to daemon
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

    // Set timeout for operations
    struct timeval timeout;
    timeout.tv_sec = IPC_TIMEOUT_MS / 1000;
    timeout.tv_usec = (IPC_TIMEOUT_MS % 1000) * 1000;

    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    return client_fd;
}

int send_daemon_request(const char *socket_path, const char *request, char *response, size_t response_size) {
    if (!socket_path || !request || !response) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) {
        return -1;
    }

    // Send request
    if (send_ipc_message(client_fd, request) == -1) {
        close(client_fd);
        return -1;
    }

    // Receive response
    int result = receive_ipc_message(client_fd, response, response_size);
    close(client_fd);

    return result;
}

int ping_daemon(const char *socket_path) {
    if (!socket_path) return -1;

    int client_fd = connect_to_daemon(socket_path);
    if (client_fd == -1) {
        return -1;
    }

    // Send ping message
    const char *ping_msg = "ping";
    if (send_ipc_message(client_fd, ping_msg) == -1) {
        close(client_fd);
        return -1;
    }

    // Receive response
    char response[256];
    int result = receive_ipc_message(client_fd, response, sizeof(response));
    close(client_fd);

    if (result > 0 && strcmp(response, "pong") == 0) {
        return 0;
    }

    return -1;
}
