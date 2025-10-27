#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/select.h>
#include <sys/time.h>

int setup_daemon_pty(daemon_pty_t *pty, const char *session_id) {
    if (!pty || !session_id) return -1;

    memset(pty, 0, sizeof(daemon_pty_t));
    pty->master_fd = -1;
    pty->slave_fd = -1;
    snprintf(pty->session_id, sizeof(pty->session_id), "%s", session_id);

    // Initialize buffer
    pty->buffer_pos = 0;
    pty->active = 0;

    // Create pseudoterminal
    if (openpty(&pty->master_fd, &pty->slave_fd, NULL, NULL, NULL) == -1) {
        perror("openpty");
        return -1;
    }

    // Fork child process
    pty->child_pid = fork();
    if (pty->child_pid == -1) {
        perror("fork");
        close(pty->master_fd);
        close(pty->slave_fd);
        return -1;
    }

    if (pty->child_pid == 0) {
        close(pty->master_fd);
        setsid();

        ioctl(pty->slave_fd, TIOCSCTTY, 0);

        setenv("SMART_CMD_DAEMON_SESSION", pty->session_id, 1);

        // Duplicate slave fd to stdin, stdout, stderr
        dup2(pty->slave_fd, STDIN_FILENO);
        dup2(pty->slave_fd, STDOUT_FILENO);
        dup2(pty->slave_fd, STDERR_FILENO);

        // Close slave fd (now duplicated)
        if (pty->slave_fd > STDERR_FILENO) {
            close(pty->slave_fd);
        }

        const char *shell = getenv("SHELL");
        if (!shell) {
            shell = "/bin/bash";
        }

        // Start the shell in interactive mode
        execl(shell, shell, "-i", (char*)NULL);

        perror("execl");
        exit(1);
    } else {
        close(pty->slave_fd);

        // Set master_fd to non-blocking
        int flags = fcntl(pty->master_fd, F_GETFL, 0);
        fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);

        pty->active = 1;
        return 0;
    }
}

void cleanup_daemon_pty(daemon_pty_t *pty) {
    if (!pty) return;

    pty->active = 0;

    if (pty->master_fd != -1) {
        close(pty->master_fd);
        pty->master_fd = -1;
    }

    if (pty->slave_fd != -1) {
        close(pty->slave_fd);
        pty->slave_fd = -1;
    }

    if (pty->child_pid > 0) {
        // Send SIGTERM to child process
        kill(pty->child_pid, SIGTERM);

        // Wait for child to terminate
        int status;
        waitpid(pty->child_pid, &status, 0);
        pty->child_pid = -1;
    }
}

int read_from_daemon_pty(daemon_pty_t *pty, char *buffer, size_t buffer_size) {
    if (!pty || !pty->active || !buffer || pty->master_fd == -1) return -1;

    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(pty->master_fd, &read_fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms timeout

    int result = select(pty->master_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (result > 0 && FD_ISSET(pty->master_fd, &read_fds)) {
        ssize_t bytes_read = read(pty->master_fd, buffer, buffer_size - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';

            // Store in internal buffer for context
            size_t space_left = sizeof(pty->buffer) - pty->buffer_pos - 1;
            if (space_left > 0) {
                size_t copy_len = ((size_t)bytes_read < space_left) ? (size_t)bytes_read : space_left;
                memcpy(pty->buffer + pty->buffer_pos, buffer, copy_len);
                pty->buffer_pos += copy_len;
                pty->buffer[pty->buffer_pos] = '\0';
            }

            // If buffer is getting full, make room
            if ((size_t)pty->buffer_pos > sizeof(pty->buffer) / 2) {
                size_t move_size = sizeof(pty->buffer) / 2;
                memmove(pty->buffer, pty->buffer + move_size,
                        (size_t)pty->buffer_pos - move_size);
                pty->buffer_pos -= (int)move_size;
            }

            return bytes_read;
        } else if (bytes_read == 0) {
            // Shell closed the connection
            pty->active = 0;
            return 0;
        }
    }

    return 0;
}

int write_to_daemon_pty(daemon_pty_t *pty, const char *data, size_t len) {
    if (!pty || !pty->active || !data || pty->master_fd == -1) return -1;

    ssize_t bytes_written = write(pty->master_fd, data, len);
    return bytes_written;
}

int get_daemon_pty_context(daemon_pty_t *pty, char *context, size_t context_size) {
    if (!pty || !pty->active || !context) return -1;

    // Copy the most recent output from the buffer
    size_t start_pos = 0;
    if ((size_t)pty->buffer_pos > context_size / 2) {
        start_pos = (size_t)pty->buffer_pos - context_size / 2;
    }

    size_t copy_len = (size_t)pty->buffer_pos - start_pos;
    if (copy_len > context_size - 1) {
        copy_len = context_size - 1;
    }

    memcpy(context, pty->buffer + start_pos, copy_len);
    context[copy_len] = '\0';

    return copy_len;
}
