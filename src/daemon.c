#define _GNU_SOURCE
#include "smart_cmd.h"
#include "defaults.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <dirent.h>

// Global daemon state
static volatile sig_atomic_t g_daemon_running = 1;

void daemon_utils_signal_handler(int signum) {
    switch (signum) {
    case SIGTERM:
    case SIGINT:
        g_daemon_running = 0;
        break;
    case SIGCHLD:
        // Handle child process termination
        wait(NULL);
        break;
    }
}

int setup_daemon_signal_handlers() {
    setup_signal_handlers(daemon_utils_signal_handler);
    return 0;
}

// generate_session_id function moved to utils.c

int secure_temp_file(char *path, size_t path_size, const char *prefix) {
    RETURN_IF_NULL(path, -1);
    RETURN_IF_NULL(prefix, -1);

    char session_id[MAX_SESSION_ID];
    if (generate_session_id(session_id, sizeof(session_id)) == -1) {
        return -1;
    }

    char session_filename[MAX_SESSION_ID + 16];
    snprintf(session_filename, sizeof(session_filename), "%s.%d", session_id, getpid());

    return generate_temp_file_path(path, path_size, prefix, session_filename);
}

int create_daemon_lock(const char *lock_file, pid_t pid) {
    return create_lock_file_with_pid(lock_file, pid);
}

int check_daemon_running(const char *lock_file) {
    RETURN_IF_NULL(lock_file, 0);

    FILE *f = fopen(lock_file, "r");
    if (!f) return 0;

    pid_t stored_pid;
    if (fscanf(f, "%d", &stored_pid) != 1) {
        fclose(f);
        return 0;
    }

    fclose(f);
    return is_process_running(stored_pid);
}

int create_daemon_lock_force(const char *lock_file, pid_t pid) {
    RETURN_IF_NULL(lock_file, -1);

    // Remove existing lock file if it exists (force overwrite)
    unlink(lock_file);

    return create_lock_file_with_pid(lock_file, pid);
}

int cleanup_daemon_lock(const char *lock_file) {
    return cleanup_lock_file(lock_file);
}

int check_safe_environment() {
    if (getenv("SMART_CMD_DAEMON_ACTIVE")) {
        fprintf(stderr, "ERROR: check_safe_environment: Already inside a daemon session, preventing nesting\n");
        return -1;
    }

    if (getuid() != geteuid()) {
        fprintf(stderr, "ERROR: check_safe_environment: Running with setuid is not allowed\n");
        return -1;
    }

    if (getenv("TMUX"))
        fprintf(stderr, "Warning: check_safe_environment: Running in tmux, PTY functionality may be limited\n");

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 256)
        fprintf(stderr, "Warning: check_safe_environment: Low file descriptor limit (%lu), may affect operation\n",
                (unsigned long)rl.rlim_cur);

    return 0;
}

static void daemon_child_process(daemon_session_t *info) {
    if (setsid() == -1) {
        perror("setsid");
        exit(1);
    }

    if (chdir("/") == -1) {
        perror("chdir");
        exit(1);
    }

    umask(077);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    if (setup_daemon_signal_handlers() == -1) {
        exit(1);
    }

    setenv("SMART_CMD_DAEMON_ACTIVE", "1", 1);

    info->daemon_pid = getpid();
    info->start_time = time(NULL);
    info->active = 1;

    while (g_daemon_running) {
        sleep(1);
    }

    cleanup_daemon_lock(info->paths.lock_file);
    unlink(info->paths.socket_path);
    exit(0);
}

static int daemon_parent_process(pid_t child_pid, daemon_session_t *info) {
    info->daemon_pid = child_pid;
    info->start_time = time(NULL);
    info->active = 1;

    usleep(DEFAULT_DAEMON_INIT_WAIT);

    if (kill(child_pid, 0) == -1) {
        fprintf(stderr, "ERROR: start_daemon_process: Daemon failed to start\n");
        info->active = 0;
        cleanup_daemon_lock(info->paths.lock_file);
        return -1;
    }

    printf("Daemon started successfully (PID: %d, Session: %s)\n",
           child_pid, info->paths.session_id);
    return 0;
}

static int setup_daemon_paths(daemon_session_t *info) {
    int err;

    if ((err = generate_session_id(info->paths.session_id, sizeof(info->paths.session_id))) != 0) return err;
    if ((err = generate_socket_path(info->paths.socket_path, sizeof(info->paths.socket_path), info->paths.session_id)) != 0) return err;
    if ((err = generate_lock_path(info->paths.lock_file, sizeof(info->paths.lock_file), info->paths.session_id)) != 0) return err;
    if ((err = generate_log_path(info->paths.log_file, sizeof(info->paths.log_file), info->paths.session_id)) != 0) return err;

    return create_daemon_lock(info->paths.lock_file, getpid());
}

int start_daemon_process(daemon_session_t *info) {
    if (!info) return -1;

    int err;
    if ((err = check_safe_environment()) != 0) return err;
    if ((err = setup_daemon_paths(info)) != 0) return err;

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        cleanup_daemon_lock(info->paths.lock_file);
        return -1;
    }

    if (pid == 0) {
        daemon_child_process(info);
    } else {
        return daemon_parent_process(pid, info);
    }

    return 0;
}

int stop_daemon_process(daemon_session_t *info) {
    if (!info || !info->active) return -1;

    // Send SIGTERM to daemon
    if (kill(info->daemon_pid, SIGTERM) == -1) {
        perror("kill");
        return -1;
    }

    // Wait for daemon to terminate
    int status;
    if (waitpid(info->daemon_pid, &status, 0) == -1) {
        perror("waitpid");
        return -1;
    }

    // Cleanup
    cleanup_daemon_lock(info->paths.lock_file);
    unlink(info->paths.socket_path);
    info->active = 0;

    printf("Daemon stopped successfully\n");
    return 0;
}

int daemon_is_active(daemon_session_t *info) {
    RETURN_IF_NULL(info, 0);

    if (!info->active) return 0;

    if (!is_process_running(info->daemon_pid)) {
        info->active = 0;
        return 0;
    }

    return 1;
}

int cleanup_old_sessions(const char *base_path, int max_age_hours) {
    if (!base_path) return -1;

    DIR *dir = opendir(base_path);
    if (!dir) return 0;

    time_t cutoff_time = time(NULL) - (max_age_hours * 3600);
    int cleaned_files = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!starts_with(entry->d_name, "smart-cmd.")) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && st.st_mtime < cutoff_time) {
            if (unlink(full_path) == 0) {
                cleaned_files++;
            }
        }
    }

    closedir(dir);
    return cleaned_files;
}