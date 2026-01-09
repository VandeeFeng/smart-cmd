#ifndef DEFAULTS_H
#define DEFAULTS_H

// Default endpoints for different LLM providers
#define DEFAULT_OPENAI_ENDPOINT "https://api.openai.com/v1/chat/completions"
#define DEFAULT_GEMINI_ENDPOINT "https://generativelanguage.googleapis.com/v1beta/models"
#define DEFAULT_OPENROUTER_ENDPOINT "https://openrouter.ai/api/v1/chat/completions"

// Command history limits
#define MAX_HISTORY_COMMANDS 50
#define DEFAULT_HISTORY_LIMIT 50
#define DEFAULT_SESSION_TIMEOUT 3600
#define DEFAULT_DAEMON_STARTUP_DELAY 500000
#define DEFAULT_DAEMON_STARTUP_ATTEMPTS 10

// Buffer sizes for LLM client
#define LLM_MAX_BUFFER 8192
#define LLM_MAX_CONTENT 4096
#define LLM_MAX_HEADERS 10
#define LLM_MAX_HEADER_LENGTH 300
#define LLM_MAX_ENDPOINT_LENGTH 512
#define LLM_MAX_SYSTEM_PROMPT_LENGTH 4096
#define LLM_MAX_PROMPT_LENGTH 4110
#define LLM_MAX_HISTORY_MESSAGES 3

// Message constants
#define MSG_CONFIG_NOT_FOUND "No configuration file found, using defaults"
#define MSG_DAEMON_START_FAILED "Failed to start daemon"
#define MSG_DAEMON_NOT_FOUND "Daemon mode enabled but daemon not available"
#define MSG_DAEMON_STARTED "Daemon started successfully"
#define MSG_DAEMON_STOPPED "Daemon stopped successfully"
#define MSG_DAEMON_READY "Daemon ready"
#define MSG_SMART_CMD_ENABLED "Smart-cmd enabled"
#define MSG_COMPLETION_DISABLED "Smart completion disabled"
#define MSG_COMPLETION_ENABLED "Smart completion enabled"

#define TEMP_DIR_PATTERN "/tmp/smart-cmd.%s"
#define LOCK_FILE_PATTERN "smart-cmd.lock.%s"
#define SOCKET_FILE_PATTERN "smart-cmd.socket.%s"
#define LOG_FILE_PATTERN "smart-cmd.log.%s"

char* get_default_bin_path(const char* binary_name);
char* get_config_file_path(void);
int get_temp_file_path(char* path, size_t path_size, const char* prefix);

#endif