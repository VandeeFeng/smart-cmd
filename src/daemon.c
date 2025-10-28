#define _GNU_SOURCE
#include "smart_cmd.h"
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
    int result = 0;
    if (fscanf(f, "%d", &stored_pid) == 1) {
        result = is_process_running(stored_pid);
    }
    fclose(f);

    return result;
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
    // Check if already inside a daemon session
    if (getenv("SMART_CMD_DAEMON_ACTIVE")) {
        fprintf(stderr, "ERROR: check_safe_environment: Already inside a daemon session, preventing nesting\n");
        return -1;
    }

    // Check if running in restricted environment
    if (getuid() != geteuid()) {
        fprintf(stderr, "ERROR: check_safe_environment: Running with setuid is not allowed\n");
        return -1;
    }

    // Check if in tmux or similar terminal multiplexer
    if (getenv("TMUX")) {
        fprintf(stderr, "Warning: check_safe_environment: Running in tmux, PTY functionality may be limited\n");
    }

    // Check resource limits
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (rl.rlim_cur < 256) {
            fprintf(stderr, "Warning: check_safe_environment: Low file descriptor limit (%lu), may affect operation\n",
                    (unsigned long)rl.rlim_cur);
        }
    }

    return 0;
}

int start_daemon_process(daemon_session_t *info) {
    if (!info) return -1;

    // Check safe environment first
    if (check_safe_environment() == -1) {
        return -1;
    }

    // Generate session ID
    int err;
    if ((err = generate_session_id(info->paths.session_id, sizeof(info->paths.session_id))) != 0) {
        fprintf(stderr, "ERROR: start_daemon_process: generate_session_id failed\n");
        return -1;
    }

    // Setup paths using individual utility functions
    if ((err = generate_socket_path(info->paths.socket_path, sizeof(info->paths.socket_path), info->paths.session_id)) != 0) {
        fprintf(stderr, "ERROR: start_daemon_process: generate_socket_path failed\n");
        return -1;
    }

    if ((err = generate_lock_path(info->paths.lock_file, sizeof(info->paths.lock_file), info->paths.session_id)) != 0) {
        fprintf(stderr, "ERROR: start_daemon_process: generate_lock_path failed\n");
        return -1;
    }

    if ((err = generate_log_path(info->paths.log_file, sizeof(info->paths.log_file), info->paths.session_id)) != 0) {
        fprintf(stderr, "ERROR: start_daemon_process: generate_log_path failed\n");
        return -1;
    }

    // Create daemon lock
    if ((err = create_daemon_lock(info->paths.lock_file, getpid())) != 0) {
        fprintf(stderr, "ERROR: start_daemon_process: create_daemon_lock failed - another instance may be running\n");
        return -1;
    }

    // Fork to create daemon
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        cleanup_daemon_lock(info->paths.lock_file);
        return -1;
    }

    if (pid == 0) {
        // Child process (daemon)
        // Create new session
        if (setsid() == -1) {
            perror("setsid");
            exit(1);
        }

        // Change working directory to root
        if (chdir("/") == -1) {
            perror("chdir");
            exit(1);
        }

        // Set umask
        umask(077);

        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Open /dev/null for standard descriptors
        open("/dev/null", O_RDONLY); // stdin
        open("/dev/null", O_WRONLY); // stdout
        open("/dev/null", O_WRONLY); // stderr

        // Setup signal handlers
        if (setup_daemon_signal_handlers() == -1) {
            exit(1);
        }

        // Set environment variable to prevent nesting
        setenv("SMART_CMD_DAEMON_ACTIVE", "1", 1);

        info->daemon_pid = getpid();
        info->start_time = time(NULL);
        info->active = 1;

        // Main daemon loop would go here
        // For now, just keep the process running
        while (g_daemon_running) {
            sleep(1);
        }

        // Cleanup
        cleanup_daemon_lock(info->paths.lock_file);
        unlink(info->paths.socket_path);
        exit(0);
    } else {
        // Parent process
        info->daemon_pid = pid;
        info->start_time = time(NULL);
        info->active = 1;

        // Give daemon a moment to start
        usleep(100000); // 100ms

        // Check if daemon is still running
        if (kill(pid, 0) == -1) {
            fprintf(stderr, "ERROR: start_daemon_process: Daemon failed to start\n");
            info->active = 0;
            cleanup_daemon_lock(info->paths.lock_file);
            return -1;
        }

        printf("Daemon started successfully (PID: %d, Session: %s)\n",
               pid, info->paths.session_id);
        return 0;
    }
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
    if (!dir) return 0; // Directory may not exist, that's OK

    time_t cutoff_time = time(NULL) - (max_age_hours * 3600);
    int cleaned_files = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Only process smart-cmd files
        if (!starts_with(entry->d_name, "smart-cmd.")) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (st.st_mtime < cutoff_time) {
                if (unlink(full_path) == 0) {
                    cleaned_files++;
                }
            }
        }
    }

    closedir(dir);
    return cleaned_files;
}