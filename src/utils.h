#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

// Constants for temporary files
#define SMART_CMD_PREFIX "smart-cmd"
#define LOCK_FILE_PREFIX "smart-cmd.lock"
#define SOCKET_FILE_PREFIX "smart-cmd.socket"
#define LOG_FILE_PREFIX "smart-cmd.log"

// Error handling macros
#define SAFE_FREE(ptr) do { if (ptr) { free(ptr); ptr = NULL; } } while(0)
#define RETURN_IF_NULL(ptr, retval) do { if (!(ptr)) { return (retval); } } while(0)
#define RETURN_IF_NEG(val, retval) do { if ((val) < 0) { return (retval); } } while(0)

// Utility functions
const char* get_smart_cmd_tmpdir(void);
int generate_session_id(char *session_id, size_t len);
void setup_signal_handlers(void (*handler)(int));

// Path generation utilities
int generate_lock_path(char *path, size_t size, const char *session_id);
int generate_socket_path(char *path, size_t size, const char *session_id);
int generate_log_path(char *path, size_t size, const char *session_id);
int generate_temp_file_path(char *path, size_t size, const char *prefix, const char *session_id);

// File operation utilities
int create_directory_if_not_exists(const char *dir_path);
int safe_write_file(const char *file_path, const char *content, int sync_to_disk);
int safe_read_file(const char *file_path, char *buffer, size_t buffer_size);

// Lock file management
int create_lock_file_with_pid(const char *lock_file, pid_t pid);
int is_process_running(pid_t pid);
int cleanup_lock_file(const char *lock_file);

// String utilities
int safe_string_copy(char *dest, const char *src, size_t dest_size);
int safe_string_append(char *dest, const char *src, size_t dest_size);
int starts_with(const char *str, const char *prefix);

// Argument processing utilities
char *concat_remaining_args(int argc, char *argv[], int start_index);

#endif // UTILS_H