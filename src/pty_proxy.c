#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/select.h>
#include <sys/time.h>

int setup_pty_proxy(pty_proxy_t *proxy) {
    if (!proxy) return -1;

    // Initialize buffer
    memset(proxy->buffer, 0, sizeof(proxy->buffer));
    proxy->buffer_pos = 0;

    // Create pseudoterminal
    if (openpty(&proxy->master_fd, &proxy->slave_fd, NULL, NULL, NULL) == -1) {
        perror("openpty");
        return -1;
    }

    // Fork child process
    proxy->child_pid = fork();
    if (proxy->child_pid == -1) {
        perror("fork");
        close(proxy->master_fd);
        close(proxy->slave_fd);
        return -1;
    }

    if (proxy->child_pid == 0) {
        // Child process - start shell
        close(proxy->master_fd);

        // Create new session
        setsid();

        // Set controlling terminal
        ioctl(proxy->slave_fd, TIOCSCTTY, 0);

        // Duplicate slave fd to stdin, stdout, stderr
        dup2(proxy->slave_fd, STDIN_FILENO);
        dup2(proxy->slave_fd, STDOUT_FILENO);
        dup2(proxy->slave_fd, STDERR_FILENO);

        // Close slave fd (now duplicated)
        if (proxy->slave_fd > STDERR_FILENO) {
            close(proxy->slave_fd);
        }

        // Get user's shell
        const char *shell = getenv("SHELL");
        if (!shell) {
            shell = "/bin/bash";
        }

        // Start the shell
        execl(shell, shell, "-i", (char*)NULL);

        // If we get here, execl failed
        perror("execl");
        exit(1);
    } else {
        // Parent process
        close(proxy->slave_fd);

        // Set master_fd to non-blocking
        int flags = fcntl(proxy->master_fd, F_GETFL, 0);
        fcntl(proxy->master_fd, F_SETFL, flags | O_NONBLOCK);

        return 0;
    }
}

void cleanup_pty_proxy(pty_proxy_t *proxy) {
    if (!proxy) return;

    if (proxy->master_fd != -1) {
        close(proxy->master_fd);
        proxy->master_fd = -1;
    }

    if (proxy->slave_fd != -1) {
        close(proxy->slave_fd);
        proxy->slave_fd = -1;
    }

    if (proxy->child_pid > 0) {
        // Send SIGTERM to child process
        kill(proxy->child_pid, SIGTERM);

        // Wait for child to terminate
        int status;
        waitpid(proxy->child_pid, &status, 0);
        proxy->child_pid = -1;
    }
}

int read_from_pty(pty_proxy_t *proxy, char *buffer, size_t buffer_size) {
    if (!proxy || !buffer || proxy->master_fd == -1) return -1;

    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(proxy->master_fd, &read_fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms timeout

    int result = select(proxy->master_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (result > 0 && FD_ISSET(proxy->master_fd, &read_fds)) {
        ssize_t bytes_read = read(proxy->master_fd, buffer, buffer_size - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';

            // Store in internal buffer for context
            size_t space_left = sizeof(proxy->buffer) - proxy->buffer_pos - 1;
            if (space_left > 0) {
                size_t copy_len = (bytes_read < space_left) ? bytes_read : space_left;
                memcpy(proxy->buffer + proxy->buffer_pos, buffer, copy_len);
                proxy->buffer_pos += copy_len;
                proxy->buffer[proxy->buffer_pos] = '\0';
            }

            // If buffer is getting full, make room
            if (proxy->buffer_pos > sizeof(proxy->buffer) / 2) {
                size_t move_size = sizeof(proxy->buffer) / 2;
                memmove(proxy->buffer, proxy->buffer + move_size,
                       proxy->buffer_pos - move_size);
                proxy->buffer_pos -= move_size;
            }

            return bytes_read;
        }
    }

    return 0;
}

int write_to_pty(pty_proxy_t *proxy, const char *data, size_t len) {
    if (!proxy || !data || proxy->master_fd == -1) return -1;

    ssize_t bytes_written = write(proxy->master_fd, data, len);
    if (bytes_written > 0) {
        // Also echo to our stdout for immediate feedback
        write(STDOUT_FILENO, data, bytes_written);
    }

    return bytes_written;
}

int get_pty_context(pty_proxy_t *proxy, char *context, size_t context_size) {
    if (!proxy || !context) return -1;

    // Copy the most recent output from the buffer
    // We want the last part of the buffer as context
    size_t start_pos = 0;
    if (proxy->buffer_pos > context_size / 2) {
        start_pos = proxy->buffer_pos - context_size / 2;
    }

    size_t copy_len = proxy->buffer_pos - start_pos;
    if (copy_len > context_size - 1) {
        copy_len = context_size - 1;
    }

    memcpy(context, proxy->buffer + start_pos, copy_len);
    context[copy_len] = '\0';

    return copy_len;
}