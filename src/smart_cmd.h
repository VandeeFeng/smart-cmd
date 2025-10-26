#ifndef SMART_CMD_H
#define SMART_CMD_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pty.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <wordexp.h>

// Constants
#define MAX_INPUT_LEN 4096
#define MAX_CONTEXT_LEN 8192
#define MAX_SUGGESTION_LEN 1024
#define CONFIG_FILE_PATH "~/.config/smart-cmd/config.json"

typedef struct {
    int master_fd;
    int slave_fd;
    pid_t child_pid;
    char buffer[MAX_CONTEXT_LEN];
    int buffer_pos;
} pty_proxy_t;

typedef struct {
    char username[64];
    char hostname[256];
    char cwd[512];
    char last_command[MAX_INPUT_LEN];
    char terminal_buffer[MAX_CONTEXT_LEN];
} context_t;

typedef struct {
    char provider[32];
    char api_key[256];
    char model[64];
    char endpoint[256];
} llm_config_t;

typedef struct {
    llm_config_t llm;
    char trigger_key[8]; // e.g., "ctrl+o"
    int trigger_key_value; // Parsed ASCII value
    int enable_proxy_mode;
} config_t;

typedef struct {
    char suggestion[MAX_SUGGESTION_LEN];
    char type; // '+' for completion, '=' for new command
    int visible;
} suggestion_t;

// Function prototypes
int init_input_capture();
void restore_terminal();
int capture_input(char *input, size_t max_len, const config_t *config);
int setup_pty_proxy(pty_proxy_t *proxy);
void cleanup_pty_proxy(pty_proxy_t *proxy);
int read_from_pty(pty_proxy_t *proxy, char *buffer, size_t buffer_size);
int write_to_pty(pty_proxy_t *proxy, const char *data, size_t len);
int get_pty_context(pty_proxy_t *proxy, char *context, size_t context_size);
int collect_context(context_t *ctx, pty_proxy_t *proxy);
int send_to_llm(const char *input, const context_t *ctx, const config_t *config, suggestion_t *suggestion);
int render_suggestion(const suggestion_t *suggestion, const char *current_input);
int show_completion_indicator(int show);
int display_help();
int display_error(const char *message);
int accept_suggestion(char *current_input, const suggestion_t *suggestion);
int load_config(config_t *config);
int check_trigger_key(const config_t *config, char first_char);
int handle_escape_sequence(void);
void signal_handler(int signum);

#endif // SMART_CMD_H
