#define _GNU_SOURCE
#include "smart_cmd.h"
#include <getopt.h>

static struct termios orig_termios;
static volatile sig_atomic_t running = 1;
static suggestion_t current_suggestion;
static char current_input[MAX_INPUT_LEN];

void signal_handler(int signum) {
    switch (signum) {
    case SIGINT:
    case SIGTERM:
        running = 0;
        break;
    case SIGCHLD:
        // Handle child process termination
        wait(NULL);
        break;
    }
}

int setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

int init_input_capture() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        return -1;
    }

    struct termios new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO | ISIG);
    new_termios.c_iflag &= ~(IXON | IXOFF | ICRNL);
    new_termios.c_oflag &= ~OPOST;
    new_termios.c_cc[VMIN] = 1;
    new_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

int capture_input(char *input, size_t max_len, const config_t *config) {
    char ch;
    size_t pos = strlen(input);

    while (running && pos < max_len - 1) {
        if (read(STDIN_FILENO, &ch, 1) <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (check_trigger_key(config, ch)) {
            return 1;
        }

        if (ch == 27) {
            int result = handle_escape_sequence();
            if (result != -1) {
                return result;
            }
        }

        if (ch == '\n' || ch == '\r') {
            input[pos] = '\0';
            write(STDOUT_FILENO, "\r\n", 2);
            return 0;
        }

        if (ch == 127 || ch == 8) { // Backspace
            if (pos > 0) {
                pos--;
                input[pos] = '\0';
                write(STDOUT_FILENO, "\b \b", 3);

                // Clear and redraw suggestion
                current_suggestion.visible = 0;
                render_suggestion(&current_suggestion, input);
            }
            continue;
        }

        if (isprint(ch)) {
            input[pos++] = ch;
            input[pos] = '\0';
            write(STDOUT_FILENO, &ch, 1);

            // Update suggestion when user types
            current_suggestion.visible = 0;
            render_suggestion(&current_suggestion, input);
        }
    }

    input[pos] = '\0';
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -t, --test     Run basic tests\n");
    printf("  -v, --version  Show version information\n");
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"help",    no_argument, 0, 'h'},
        {"test",    no_argument, 0, 't'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "htv", long_options, &option_index)) != -1) {
        switch (c) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 't':
            printf("Running basic tests...\n");
            // TODO: Implement test suite
            return 0;
        case 'v':
            printf("smart-cmd version 1.0.0\n");
            return 0;
        case '?':
            return 1;
        default:
            abort();
        }
    }

    printf("Smart Command Completion Tool v1.0.0\n");
    printf("Press Ctrl+O to trigger AI-powered completion\n");
    printf("Press ESC or Ctrl+C to exit\n\n");

    display_help();

    if (setup_signal_handlers() == -1) {
        fprintf(stderr, "Failed to setup signal handlers\n");
        return 1;
    }

    if (init_input_capture() == -1) {
        fprintf(stderr, "Failed to initialize input capture\n");
        return 1;
    }

    config_t config;
    pty_proxy_t proxy = {0};

    if (load_config(&config) == -1) {
        fprintf(stderr, "Warning: Could not load config, using defaults\n");
        strcpy(config.llm.provider, "openai");
        strcpy(config.trigger_key, "ctrl+o");
        config.trigger_key_value = 15;
        config.enable_proxy_mode = 1;
    }

    // Initialize PTY proxy if enabled
    if (config.enable_proxy_mode) {
        if (setup_pty_proxy(&proxy) == -1) {
            fprintf(stderr, "Warning: Failed to setup PTY proxy, continuing without it\n");
        }
    }

    // Initialize current input and suggestion
    memset(current_input, 0, sizeof(current_input));
    memset(&current_suggestion, 0, sizeof(suggestion_t));

    printf("$ ");
    fflush(stdout);

    while (running) {
        int result = capture_input(current_input, sizeof(current_input), &config);

        if (result == 1) {
            if (strlen(current_input) > 0) {
                show_completion_indicator(1);
                fflush(stdout);

                context_t ctx;
                collect_context(&ctx, config.enable_proxy_mode ? &proxy : NULL);

                // Send to LLM
                if (send_to_llm(current_input, &ctx, &config, &current_suggestion) == 0) {
                    // Display suggestion
                    render_suggestion(&current_suggestion, current_input);
                } else {
                    display_error("Failed to get AI completion");
                    render_suggestion(&current_suggestion, current_input);
                }

                show_completion_indicator(0);
                fflush(stdout);
            }
        } else if (result == 2) {
            if (current_suggestion.visible) {
                accept_suggestion(current_input, &current_suggestion);
                current_suggestion.visible = 0;
            }
        } else if (result == 0) {
            current_suggestion.visible = 0;
            if (strlen(current_input) > 0) {
                // Execute command (in a real implementation, need to handle this better)
                printf("Executing: %s\n", current_input);

                // Send to PTY proxy if available
                if (config.enable_proxy_mode && proxy.master_fd != -1) {
                    char cmd_with_newline[MAX_INPUT_LEN + 2];
                    snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", current_input);
                    write_to_pty(&proxy, cmd_with_newline, strlen(cmd_with_newline));

                    // Read some output from PTY
                    char output[1024];
                    int bytes_read = read_from_pty(&proxy, output, sizeof(output));
                    if (bytes_read > 0) {
                        printf("%s", output);
                    }
                }

                memset(current_input, 0, sizeof(current_input));
            }

            printf("$ ");
            fflush(stdout);
        } else if (result == -1) { // Other special key
            continue;
        }

        if (!running) break;
    }

    if (config.enable_proxy_mode && proxy.master_fd != -1) {
        cleanup_pty_proxy(&proxy);
    }

    restore_terminal();
    printf("\nGoodbye!\n");
    return 0;
}
