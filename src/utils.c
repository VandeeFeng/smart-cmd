#define _GNU_SOURCE
#include "utils.h"
#include "smart_cmd.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>

// Forward declarations
static int is_process_running_from_lock(const char *lock_file);

// Global temporary directory cache
static char g_tmpdir_cache[256] = {0};

const char* get_smart_cmd_tmpdir(void) {
    if (g_tmpdir_cache[0] == '\0') {
        const char *tmp_dir = getenv("TMPDIR");
        if (!tmp_dir || strlen(tmp_dir) == 0) {
            tmp_dir = "/tmp";
        }
        strncpy(g_tmpdir_cache, tmp_dir, sizeof(g_tmpdir_cache) - 1);
        g_tmpdir_cache[sizeof(g_tmpdir_cache) - 1] = '\0';
    }
    return g_tmpdir_cache;
}

int generate_session_id(char *session_id, size_t len) {
    RETURN_IF_NULL(session_id, -1);
    if (len < 17) return -1;

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        return -1;
    }

    unsigned long hash = ((unsigned long)ts.tv_sec ^ ts.tv_nsec) ^ getpid();
    snprintf(session_id, len, "%016lx", hash);
    return 0;
}

void setup_signal_handlers(void (*handler)(int)) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}

int generate_lock_path(char *path, size_t size, const char *session_id) {
    RETURN_IF_NULL(path, -1);
    RETURN_IF_NULL(session_id, -1);

    const char *tmpdir = get_smart_cmd_tmpdir();
    snprintf(path, size, "%s/%s.%s", tmpdir, LOCK_FILE_PREFIX, session_id);
    return 0;
}

int generate_session_paths(void *paths, const char *session_id) {
    RETURN_IF_NULL(paths, -1);
    RETURN_IF_NULL(session_id, -1);

    // Cast to the actual structure type
    session_paths_t *session_paths = (session_paths_t*)paths;

    if (generate_socket_path(session_paths->socket_path, sizeof(session_paths->socket_path), session_id) == -1) {
        return -1;
    }

    if (generate_lock_path(session_paths->lock_file, sizeof(session_paths->lock_file), session_id) == -1) {
        return -1;
    }

    if (generate_log_path(session_paths->log_file, sizeof(session_paths->log_file), session_id) == -1) {
        return -1;
    }

    strncpy(session_paths->session_id, session_id, sizeof(session_paths->session_id) - 1);
    session_paths->session_id[sizeof(session_paths->session_id) - 1] = '\0';

    return 0;
}

int generate_socket_path(char *path, size_t size, const char *session_id) {
    RETURN_IF_NULL(path, -1);
    RETURN_IF_NULL(session_id, -1);

    const char *tmpdir = get_smart_cmd_tmpdir();
    snprintf(path, size, "%s/%s.%s", tmpdir, SOCKET_FILE_PREFIX, session_id);
    return 0;
}

int generate_log_path(char *path, size_t size, const char *session_id) {
    RETURN_IF_NULL(path, -1);
    RETURN_IF_NULL(session_id, -1);

    const char *tmpdir = get_smart_cmd_tmpdir();
    snprintf(path, size, "%s/%s.%s", tmpdir, LOG_FILE_PREFIX, session_id);
    return 0;
}

int generate_temp_file_path(char *path, size_t size, const char *prefix, const char *session_id) {
    RETURN_IF_NULL(path, -1);
    RETURN_IF_NULL(prefix, -1);
    RETURN_IF_NULL(session_id, -1);

    const char *tmpdir = get_smart_cmd_tmpdir();
    snprintf(path, size, "%s/%s.%s.%s", tmpdir, SMART_CMD_PREFIX, prefix, session_id);
    return 0;
}

int create_directory_if_not_exists(const char *dir_path) {
    RETURN_IF_NULL(dir_path, -1);

    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
        if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

int safe_write_file(const char *file_path, const char *content, int sync_to_disk) {
    RETURN_IF_NULL(file_path, -1);
    RETURN_IF_NULL(content, -1);

    int fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) return -1;

    size_t content_len = strlen(content);
    ssize_t written = write(fd, content, content_len);

    if (sync_to_disk && fsync(fd) == -1) {
        close(fd);
        return -1;
    }

    close(fd);
    return (written == (ssize_t)content_len) ? 0 : -1;
}

int safe_read_file(const char *file_path, char *buffer, size_t buffer_size) {
    RETURN_IF_NULL(file_path, -1);
    RETURN_IF_NULL(buffer, -1);

    FILE *fp = fopen(file_path, "r");
    if (!fp) return -1;

    size_t read_size = fread(buffer, 1, buffer_size - 1, fp);
    buffer[read_size] = '\0';

    fclose(fp);
    return (read_size > 0) ? 0 : -1;
}

int create_lock_file_with_pid(const char *lock_file, pid_t pid) {
    RETURN_IF_NULL(lock_file, -1);

    // Create directory if needed
    char *lock_dir = strdup(lock_file);
    if (!lock_dir) return -1;

    char *last_slash = strrchr(lock_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (create_directory_if_not_exists(lock_dir) == -1) {
            free(lock_dir);
            return -1;
        }
    }
    free(lock_dir);

    // Try to create lock file exclusively
    int fd = open(lock_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd == -1) {
        if (errno == EEXIST) {
            // Check if process is still running
            if (is_process_running_from_lock(lock_file)) {
                return -1; // Another process is running
            }
            // Stale lock file, remove it
            unlink(lock_file);
            fd = open(lock_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
            if (fd == -1) return -1;
        } else {
            return -1;
        }
    }

    // Write PID to lock file
    char pid_str[32];
    int pid_len = snprintf(pid_str, sizeof(pid_str), "%d\n", pid);
    int result = (write(fd, pid_str, pid_len) == pid_len) ? 0 : -1;

    if (fsync(fd) == -1) result = -1;
    close(fd);

    if (result != 0) {
        unlink(lock_file);
    }

    return result;
}

int is_process_running(pid_t pid) {
    return (kill(pid, 0) == 0) ? 1 : 0;
}

int cleanup_lock_file(const char *lock_file) {
    RETURN_IF_NULL(lock_file, -1);
    return unlink(lock_file);
}

int safe_string_copy(char *dest, const char *src, size_t dest_size) {
    RETURN_IF_NULL(dest, -1);
    RETURN_IF_NULL(src, -1);
    if (dest_size == 0) return -1;

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return 0;
}

int safe_string_append(char *dest, const char *src, size_t dest_size) {
    RETURN_IF_NULL(dest, -1);
    RETURN_IF_NULL(src, -1);

    size_t dest_len = strlen(dest);
    if (dest_len >= dest_size - 1) return -1;

    strncat(dest, src, dest_size - dest_len - 1);
    return 0;
}

int starts_with(const char *str, const char *prefix) {
    RETURN_IF_NULL(str, 0);
    RETURN_IF_NULL(prefix, 0);

    return strncmp(str, prefix, strlen(prefix)) == 0;
}

// Helper function to check if process is running from lock file
static int is_process_running_from_lock(const char *lock_file) {
    FILE *fp = fopen(lock_file, "r");
    if (!fp) return 0;

    pid_t stored_pid;
    int result = (fscanf(fp, "%d", &stored_pid) == 1 && is_process_running(stored_pid)) ? 1 : 0;

    fclose(fp);
    return result;
}

char *concat_remaining_args(int argc, char *argv[], int start_index) {
    if (start_index >= argc) return NULL;

    size_t total_len = 0;
    for (int i = start_index; i < argc; i++) {
        total_len += strlen(argv[i]) + 1;
    }

    char *result = malloc(total_len);
    if (!result) return NULL;

    result[0] = '\0';
    for (int i = start_index; i < argc; i++) {
        strcat(result, argv[i]);
        if (i < argc - 1) {
            strcat(result, " ");
        }
    }

    return result;
}