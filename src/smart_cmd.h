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
#define MAX_HISTORY_COMMANDS 50



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
    int show_startup_messages;
} config_t;

typedef struct {
    char suggestion[MAX_SUGGESTION_LEN];
    char type; // '+' for completion, '=' for new command
    int visible;
} suggestion_t;

typedef struct {
    int master_fd;
    int slave_fd;
    pid_t child_pid;
    char buffer[MAX_CONTEXT_LEN];
    int buffer_pos;
    int active;
    char session_id[32];
} daemon_pty_t;

typedef struct {
    pid_t daemon_pid;
    char socket_path[256];
    char lock_file[256];
    char session_id[32];
    char log_file[256];
    int active;
    time_t start_time;
} daemon_info_t;

// Command history structure
typedef struct {
    char command[MAX_INPUT_LEN];
    time_t timestamp;
} command_history_t;

typedef struct {
    command_history_t commands[50];  // Store last 50 commands
    int count;
    int current_index;  // Circular buffer index
    char history_file[256];
} command_history_manager_t;

typedef struct {
    const char *command;
    const char *input;
    const char *context;
    int show_help;
    int show_version;
    int run_tests;
    int show_config;
} command_args_t;

// Function prototypes
int collect_context(context_t *ctx);
int send_to_llm(const char *input, const context_t *ctx, const config_t *config, suggestion_t *suggestion);
int load_config(config_t *config);

// Management and UI functions
int find_running_daemon(daemon_info_t *info);
int cmd_toggle();
int cmd_status();
int cmd_start();
int cmd_stop();
int cmd_mode();
void show_config();

// Print functions for main binary only
#ifndef DAEMON_BINARY
void print_usage(const char *program_name);
void print_version();
#endif

// Completion functions
int parse_completion_context(const char *context_json, context_t *ctx);
int format_completion_output(const suggestion_t *suggestion, char **json_output);
int run_basic_tests();
int run_completion_mode(const char *input, const char *context_json);

// Argument processing functions
char *concat_remaining_args(int argc, char *argv[], int start_index);
int parse_command_args(int argc, char *argv[], command_args_t *args);

// Daemon-related functions
int create_daemon_lock(const char *lock_file, pid_t pid);
int create_daemon_lock_force(const char *lock_file, pid_t pid);
int check_daemon_running(const char *lock_file);
int cleanup_daemon_lock(const char *lock_file);
int generate_session_id(char *session_id, size_t len);
int start_daemon_process(daemon_info_t *info);
int stop_daemon_process(daemon_info_t *info);
int daemon_is_active(daemon_info_t *info);
int setup_daemon_pty(daemon_pty_t *pty, const char *session_id);
void cleanup_daemon_pty(daemon_pty_t *pty);
int read_from_daemon_pty(daemon_pty_t *pty, char *buffer, size_t buffer_size);
int write_to_daemon_pty(daemon_pty_t *pty, const char *data, size_t len);
int get_daemon_pty_context(daemon_pty_t *pty, char *context, size_t context_size);

// IPC-related functions
int create_ipc_socket(const char *socket_path);
int accept_ipc_connection(int server_fd);
int send_ipc_message(int fd, const char *message);
int receive_ipc_message(int fd, char *buffer, size_t buffer_size);
void cleanup_ipc_socket(const char *socket_path);

// Security functions
int check_safe_environment();
int validate_ipc_message(const char *message);
int secure_temp_file(char *path, size_t path_size, const char *prefix);
int cleanup_old_sessions(const char *base_path, int max_age_hours);

// Command history functions
int init_command_history(command_history_manager_t *manager, const char *session_id);
void cleanup_command_history(command_history_manager_t *manager);
int add_command_to_history(command_history_manager_t *manager, const char *command);
int get_recent_commands(command_history_manager_t *manager, char *recent_commands, int count, time_t max_age);
int save_command_history(command_history_manager_t *manager);
int load_command_history(command_history_manager_t *manager);

#endif // SMART_CMD_H
