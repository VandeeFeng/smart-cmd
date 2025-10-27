#ifndef DEFAULTS_H
#define DEFAULTS_H

#define DEFAULT_OPENAI_ENDPOINT "https://api.openai.com/v1/chat/completions"
#define DEFAULT_GEMINI_ENDPOINT "https://generativelanguage.googleapis.com/v1beta/models"
#define DEFAULT_OPENROUTER_ENDPOINT "https://openrouter.ai/api/v1/chat/completions"

#define DEFAULT_HISTORY_LIMIT 50
#define DEFAULT_SESSION_TIMEOUT 3600
#define DEFAULT_DAEMON_STARTUP_ATTEMPTS 10
#define DEFAULT_DAEMON_STARTUP_DELAY 500000

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
char* get_temp_file_path(const char* prefix);

#endif