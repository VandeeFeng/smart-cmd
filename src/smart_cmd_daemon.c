#define _GNU_SOURCE
#include "smart_cmd.h"
#include <getopt.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#define MAX_IPC_MESSAGE_SIZE 4096

static daemon_session_t g_daemon_info = {0};
static daemon_pty_t g_daemon_pty = {0};
static command_history_manager_t g_command_history = {0};
static volatile sig_atomic_t g_running = 1;

void daemon_signal_handler(int signum) {
    switch (signum) {
    case SIGTERM:
    case SIGINT:
        g_running = 0;
        break;
    case SIGCHLD:
        // Handle child process termination
        wait(NULL);
        break;
    }
}

int setup_daemon_main_signal_handlers() {
    setup_signal_handlers(daemon_signal_handler);
    return 0;
}

static void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Smart Command Daemon - Provides PTY context for bash completion\n\n");
    printf("Options:\n");
    printf("  -h, --help        Show this help message\n");
    printf("  -s, --stop        Stop running daemon\n");
    printf("  -k, --status      Show daemon status\n");
    printf("  -v, --version     Show version information\n");
    printf("  -d, --debug       Enable debug logging\n");
}

static void print_version() {
    printf("smart-cmd-daemon %s\n", VERSION);
}

int find_daemon_info(daemon_session_t *info) {
    // Look for lock files in temp directory
    const char *tmp_dir = get_smart_cmd_tmpdir();

    DIR *dir = opendir(tmp_dir);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (starts_with(entry->d_name, LOCK_FILE_PREFIX ".")) {
            char lock_path[MAX_PATH];
            snprintf(lock_path, sizeof(lock_path), "%s/%s", tmp_dir, entry->d_name);

            FILE *f = fopen(lock_path, "r");
            if (f) {
                pid_t pid;
                if (fscanf(f, "%d", &pid) == 1) {
                    if (is_process_running(pid)) {
                        // Found running daemon
                        info->daemon_pid = pid;
                        safe_string_copy(info->paths.lock_file, lock_path, sizeof(info->paths.lock_file));

                        // Extract session ID from lock file name (smart-cmd.lock.{session_id})
                        const char *session_id = strrchr(entry->d_name, '.');
                        if (session_id && strlen(session_id + 1) > 0) {
                            safe_string_copy(info->paths.session_id, session_id + 1,
                                          sizeof(info->paths.session_id));
                            generate_socket_path(info->paths.socket_path, sizeof(info->paths.socket_path),
                                              info->paths.session_id);
                        } else {
                            // Fallback: try PID-based socket path
                            char pid_session[32];
                            snprintf(pid_session, sizeof(pid_session), "%d", pid);
                            generate_socket_path(info->paths.socket_path, sizeof(info->paths.socket_path),
                                              pid_session);
                        }

                        fclose(f);
                        closedir(dir);
                        return 0;
                    }
                }
                fclose(f);
            }
        }
    }

    closedir(dir);
    return -1;
}

int daemon_status() {
    daemon_session_t info;
    memset(&info, 0, sizeof(info));

    if (find_daemon_info(&info) == 0) {
        printf("Daemon is running (PID: %d)\n", info.daemon_pid);
        printf("Socket: %s\n", info.paths.socket_path);
        printf("Lock: %s\n", info.paths.lock_file);
        return 0;
    } else {
        printf("Daemon is not running\n");
        return 1;
    }
}

int daemon_stop() {
    daemon_session_t info;
    memset(&info, 0, sizeof(info));

    if (find_daemon_info(&info) == -1) {
        printf("Daemon is not running\n");
        return 1;
    }

    printf("Stopping daemon (PID: %d)... ", info.daemon_pid);
    fflush(stdout);

    if (kill(info.daemon_pid, SIGTERM) == -1) {
        perror("kill");
        printf("failed\n");
        return 1;
    }

    // Wait for daemon to stop
    int attempts = 10;
    while (attempts-- > 0) {
        if (!is_process_running(info.daemon_pid)) {
            // Daemon stopped
            printf("stopped\n");
            cleanup_lock_file(info.paths.lock_file);
            unlink(info.paths.socket_path);
            return 0;
        }
        sleep(1);
    }

    // Force kill if still running
    printf("force killing... ");
    fflush(stdout);
    if (kill(info.daemon_pid, SIGKILL) == -1) {
        perror("kill");
        printf("failed\n");
        return 1;
    }

    // Wait a bit more
    sleep(1);
    if (kill(info.daemon_pid, 0) == -1) {
        printf("killed\n");
        cleanup_daemon_lock(info.paths.lock_file);
        unlink(info.paths.socket_path);
        return 0;
    }

    printf("failed to kill\n");
    return 1;
}

int daemon_main_loop(int server_fd, int debug) {
    if (debug) {
        printf("Daemon main loop started (server_fd: %d)\n", server_fd);
    }

    while (g_running) {
        // Handle incoming IPC connections
        int client_fd = accept_ipc_connection(server_fd);
        if (client_fd > 0) {
            if (debug) {
                printf("Accepted client connection\n");
            }

            char request[MAX_IPC_MESSAGE_SIZE];
            int result = receive_ipc_message(client_fd, request, sizeof(request));
            if (result > 0) {
                if (debug) {
                    printf("Received request: %s\n", request);
                }

                char response[MAX_IPC_MESSAGE_SIZE];
                memset(response, 0, sizeof(response));

                // Process request
                if (strcmp(request, "ping") == 0) {
                    snprintf(response, sizeof(response), "%s", "pong");
                } else if (strncmp(request, "suggestion:", 11) == 0) {
                    printf("Received suggestion request: %s\n", request);
                    // Extract input from request: suggestion:<command>
                    const char *input = request + 11;
                    printf("Parsed Input: %s\n", input);

                    // Add command to history
                    add_command_to_history(&g_command_history, input);

                    // Use PTY buffer for context if available
                    session_context_t ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    if (g_daemon_pty.active) {
                        get_daemon_pty_context(&g_daemon_pty, ctx.terminal_buffer, sizeof(ctx.terminal_buffer));
                    }

                    printf("Context before LLM call:\n");
                    printf("  Terminal Buffer: <start>%s<end>\n", ctx.terminal_buffer);
                    fflush(stdout);

                    // Add recent command history to the end of the context if there's space
                    char recent_commands[1024] = {0};
                    if (get_recent_commands(&g_command_history, recent_commands, 3, 3600) > 0) {
                        size_t current_len = strlen(ctx.terminal_buffer);
                        snprintf(ctx.terminal_buffer + current_len, sizeof(ctx.terminal_buffer) - current_len,
                                 "\n\nRecent user commands:\n%s", recent_commands);
                    }

                    // Load config and get suggestion
                    config_t config;
                    if (load_config(&config) == 0) {
                        suggestion_t suggestion;
                        if (send_to_llm(input, &ctx, &config, &suggestion) == 0) {
                            snprintf(response, sizeof(response), "%c%s",
                                   suggestion.type, suggestion.suggestion);
                        } else {
                            snprintf(response, sizeof(response), "%s", "error:Failed to get AI suggestion");
                        }
                    } else {
                        snprintf(response, sizeof(response), "%s", "error:Failed to load configuration");
                    }
                } else if (strncmp(request, "context", 7) == 0) {
                    // Return current context
                    if (g_daemon_pty.active) {
                        char pty_context[MAX_CONTEXT_LEN];
                        if (get_daemon_pty_context(&g_daemon_pty, pty_context, sizeof(pty_context)) > 0) {
                            // Truncate context to fit in response buffer
                            snprintf(response, sizeof(response), "%.4000s", pty_context);
                        }
                    } else {
                        snprintf(response, sizeof(response), "%s", "error:No active PTY session");
                    }
                } else {
                    snprintf(response, sizeof(response), "%s", "error:Unknown request");
                }

                if (debug) {
                    printf("Sending response: %s\n", response);
                }

                // Send response
                if (send_ipc_message(client_fd, response) == -1) {
                    if (debug) {
                        printf("Failed to send response\n");
                    }
                }
            }

            close(client_fd);
        } else if (client_fd == -1) {
            if (debug) {
                printf("Failed to accept connection (real error)\n");
            }
            // Don't spam the log - sleep briefly to prevent rapid error loops
            usleep(100000); // 100ms
        }

        // Read from PTY if active
        if (g_daemon_pty.active) {
            char buffer[1024];
            int bytes_read = read_from_daemon_pty(&g_daemon_pty, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                if (debug) {
                    printf("PTY output: %.100s%s\n",
                           buffer, bytes_read > 100 ? "..." : "");
                }
                // PTY output is automatically stored in the internal buffer
            } else if (bytes_read == 0) {
                if (debug) {
                    printf("PTY session ended\n");
                }
                cleanup_daemon_pty(&g_daemon_pty);
            }
        }

        // Small sleep to prevent busy waiting
        usleep(10000); // 10ms
    }

    return 0;
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"help",    no_argument, 0, 'h'},
        {"stop",    no_argument, 0, 's'},
        {"status",  no_argument, 0, 'k'},
        {"version", no_argument, 0, 'v'},
        {"debug",   no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;
    int debug = 0;
    int stop_mode = 0;
    int status_mode = 0;

    while ((c = getopt_long(argc, argv, "hskvd", long_options, &option_index)) != -1) {
        switch (c) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 's':
            stop_mode = 1;
            break;
        case 'k':
            status_mode = 1;
            break;
        case 'v':
            print_version();
            return 0;
        case 'd':
            debug = 1;
            break;
        case '?':
            fprintf(stderr, "Unknown option. Use -h for help.\n");
            return 1;
        default:
            abort();
        }
    }

    if (status_mode) {
        return daemon_status();
    }

    if (stop_mode) {
        return daemon_stop();
    }

    // Check if already running
    if (daemon_status() == 0) {
        printf("Daemon is already running. Use --stop to stop it.\n");
        return 1;
    }

    if (check_safe_environment() == -1) {
        return 1;
    }

    // Generate session ID early for logging
    char session_id[32];
    if (generate_session_id(session_id, sizeof(session_id)) == -1) {
        fprintf(stderr, "Failed to generate session ID\n");
        return 1;
    }

    // Setup paths
    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";
    char log_file_path[512];
    snprintf(log_file_path, sizeof(log_file_path), "%s/smart-cmd.log.%s", tmp_dir, session_id);

    // Fork to background
    pid_t fork_pid = fork();
    if (fork_pid == -1) {
        perror("fork");
        return 1;
    }

    if (fork_pid != 0) {
        // Parent process exits
        return 0;
    }

    // Child process continues as daemon
    umask(0);
    if (setsid() == -1) {
        exit(EXIT_FAILURE);
    }
    if (chdir("/") == -1) {
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Open log file and redirect stdout/stderr
    int log_fd = open(log_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd != -1) {
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        if (log_fd > 2) {
            close(log_fd);
        }
    }

    printf("Starting Smart Command Daemon v%s\n", VERSION);
    fflush(stdout);

    // Now that we are daemonized, continue with setup
    g_daemon_info.daemon_pid = getpid();
    strncpy(g_daemon_info.paths.session_id, session_id, sizeof(g_daemon_info.paths.session_id));

    // Setup signal handlers
    setup_daemon_main_signal_handlers();

    // Finish setting up paths
    snprintf(g_daemon_info.paths.socket_path, sizeof(g_daemon_info.paths.socket_path),
            "%s/smart-cmd.socket.%s", tmp_dir, g_daemon_info.paths.session_id);
    snprintf(g_daemon_info.paths.lock_file, sizeof(g_daemon_info.paths.lock_file),
            "%s/smart-cmd.lock.%s", tmp_dir, g_daemon_info.paths.session_id);
    strncpy(g_daemon_info.paths.log_file, log_file_path, sizeof(g_daemon_info.paths.log_file) - 1);
    g_daemon_info.paths.log_file[sizeof(g_daemon_info.paths.log_file) - 1] = '\0'; // Ensure null termination

    // Create lock file
    FILE *lock_file = fopen(g_daemon_info.paths.lock_file, "w");
    if (lock_file) {
        fprintf(lock_file, "%d", g_daemon_info.daemon_pid);
        fclose(lock_file);
    } else {
        printf("Failed to create daemon lock file\n");
        fflush(stdout);
        return 1;
    }

    // Initialize command history
    if (init_command_history(&g_command_history, g_daemon_info.paths.session_id) == -1) {
        printf("Failed to initialize command history\n");
        fflush(stdout);
        return 1;
    }

    // Create IPC socket
    int server_fd = create_ipc_socket(g_daemon_info.paths.socket_path);
    if (server_fd == -1) {
        printf("Failed to create IPC socket\n");
        fflush(stdout);
        cleanup_daemon_pty(&g_daemon_pty);
        return 1;
    }

    // Setup PTY
    config_t config;
    if (load_config(&config) == 0 && config.enable_proxy_mode) {
        if (setup_daemon_pty(&g_daemon_pty, g_daemon_info.paths.session_id) != 0) {
            printf("Warning: Failed to setup PTY proxy, continuing without it\n");
            fflush(stdout);
        }
    }

    printf("Daemon setup complete. PID: %d, Session: %s, Socket: %s, Server FD: %d\n",
           g_daemon_info.daemon_pid, g_daemon_info.paths.session_id, g_daemon_info.paths.socket_path, server_fd);
    fflush(stdout);

    // Main daemon loop
    int result = daemon_main_loop(server_fd, debug);

    // Cleanup
    printf("Daemon shutting down...\n");
    cleanup_command_history(&g_command_history);
    cleanup_daemon_pty(&g_daemon_pty);
    close(server_fd);
    cleanup_daemon_lock(g_daemon_info.paths.lock_file);
    unlink(g_daemon_info.paths.socket_path);
    unlink(g_daemon_info.paths.log_file);

    return result;
}