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
    struct sigaction sa;
    sa.sa_handler = daemon_utils_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

int generate_session_id(char *session_id, size_t len) {
    if (!session_id || len < 17) return -1;

    // Get current time and process ID for uniqueness
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        return -1;
    }

    // Use timestamp and PID for session ID
    unsigned long hash = ((unsigned long)ts.tv_sec ^ ts.tv_nsec) ^ getpid();

    // Convert to hex string
    snprintf(session_id, len, "%016lx", hash);
    return 0;
}

int secure_temp_file(char *path, size_t path_size, const char *prefix) {
    if (!path || path_size < 64) return -1;

    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";

    char session_id[32];
    if (generate_session_id(session_id, sizeof(session_id)) == -1) {
        return -1;
    }

    snprintf(path, path_size, "%s/%s.%s.%d", tmp_dir, prefix, session_id, getpid());
    return 0;
}

int create_daemon_lock(const char *lock_file, pid_t pid) {
    if (!lock_file) return -1;

    // Create directory for lock file if needed
    char *lock_dir = strdup(lock_file);
    if (!lock_dir) return -1;

    char *last_slash = strrchr(lock_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (mkdir(lock_dir, 0755) == -1 && errno != EEXIST) {
            free(lock_dir);
            perror("mkdir");
            return -1;
        }
    }
    free(lock_dir);

    // Try to create lock file exclusively
    int fd = open(lock_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd == -1) {
        if (errno == EEXIST) {
            // Lock file exists, check if process is still running
            if (check_daemon_running(lock_file)) {
                return -1; // Another daemon is running
            }
            // Stale lock file, remove it
            unlink(lock_file);
            fd = open(lock_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
            if (fd == -1) {
                perror("open lock file");
                return -1;
            }
        } else {
            perror("open lock file");
            return -1;
        }
    }

    // Write PID to lock file
    char pid_str[32];
    int pid_len = snprintf(pid_str, sizeof(pid_str), "%d\n", pid);
    if (write(fd, pid_str, pid_len) != pid_len) {
        close(fd);
        unlink(lock_file);
        perror("write PID to lock file");
        return -1;
    }

    // Sync to ensure PID is written to disk
    if (fsync(fd) == -1) {
        close(fd);
        unlink(lock_file);
        perror("fsync lock file");
        return -1;
    }

    close(fd);
    return 0;
}

int check_daemon_running(const char *lock_file) {
    if (!lock_file) return 0;

    FILE *f = fopen(lock_file, "r");
    if (!f) return 0;

    pid_t stored_pid;
    if (fscanf(f, "%d", &stored_pid) != 1) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Check if process is still running
    if (kill(stored_pid, 0) == 0) {
        return 1; // Process is running
    }

    return 0; // Process is not running
}

int create_daemon_lock_force(const char *lock_file, pid_t pid) {
    if (!lock_file) return -1;

    // Create directory for lock file if needed
    char *lock_dir = strdup(lock_file);
    if (!lock_dir) return -1;

    char *last_slash = strrchr(lock_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (mkdir(lock_dir, 0755) == -1 && errno != EEXIST) {
            free(lock_dir);
            perror("mkdir");
            return -1;
        }
    }
    free(lock_dir);

    // Remove existing lock file if it exists (force overwrite)
    unlink(lock_file);

    // Create new lock file
    int fd = open(lock_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd == -1) {
        perror("open lock file");
        return -1;
    }

    // Write PID to lock file
    char pid_str[32];
    int pid_len = snprintf(pid_str, sizeof(pid_str), "%d\n", pid);
    if (write(fd, pid_str, pid_len) != pid_len) {
        close(fd);
        unlink(lock_file);
        perror("write PID to lock file");
        return -1;
    }

    close(fd);
    return 0;
}

int cleanup_daemon_lock(const char *lock_file) {
    if (!lock_file) return -1;
    return unlink(lock_file);
}

int check_safe_environment() {
    // Check if already inside a daemon session
    if (getenv("SMART_CMD_DAEMON_ACTIVE")) {
        fprintf(stderr, "Already inside a daemon session, preventing nesting\n");
        return -1;
    }

    // Check if running in restricted environment
    if (getuid() != geteuid()) {
        fprintf(stderr, "Running with setuid is not allowed\n");
        return -1;
    }

    // Check if in tmux or similar terminal multiplexer
    if (getenv("TMUX")) {
        fprintf(stderr, "Warning: Running in tmux, PTY functionality may be limited\n");
    }

    // Check resource limits
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (rl.rlim_cur < 256) {
            fprintf(stderr, "Warning: Low file descriptor limit (%lu), may affect operation\n",
                    (unsigned long)rl.rlim_cur);
        }
    }

    return 0;
}

int start_daemon_process(daemon_info_t *info) {
    if (!info) return -1;

    // Check safe environment first
    if (check_safe_environment() == -1) {
        return -1;
    }

    // Generate session ID
    if (generate_session_id(info->session_id, sizeof(info->session_id)) == -1) {
        fprintf(stderr, "Failed to generate session ID\n");
        return -1;
    }

    // Setup socket path
    if (secure_temp_file(info->socket_path, sizeof(info->socket_path), "smart-cmd.socket") == -1) {
        fprintf(stderr, "Failed to create socket path\n");
        return -1;
    }

    // Setup lock file path
    if (secure_temp_file(info->lock_file, sizeof(info->lock_file), "smart-cmd.lock") == -1) {
        fprintf(stderr, "Failed to create lock file path\n");
        return -1;
    }

    // Setup log file path
    if (secure_temp_file(info->log_file, sizeof(info->log_file), "smart-cmd.log") == -1) {
        fprintf(stderr, "Failed to create log file path\n");
        return -1;
    }

    // Create daemon lock
    if (create_daemon_lock(info->lock_file, getpid()) == -1) {
        fprintf(stderr, "Failed to create daemon lock - another instance may be running\n");
        return -1;
    }

    // Fork to create daemon
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        cleanup_daemon_lock(info->lock_file);
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
        cleanup_daemon_lock(info->lock_file);
        unlink(info->socket_path);
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
            fprintf(stderr, "Daemon failed to start\n");
            info->active = 0;
            cleanup_daemon_lock(info->lock_file);
            return -1;
        }

        printf("Daemon started successfully (PID: %d, Session: %s)\n",
               pid, info->session_id);
        return 0;
    }
}

int stop_daemon_process(daemon_info_t *info) {
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
    cleanup_daemon_lock(info->lock_file);
    unlink(info->socket_path);
    info->active = 0;

    printf("Daemon stopped successfully\n");
    return 0;
}

int daemon_is_active(daemon_info_t *info) {
    if (!info) return 0;

    if (!info->active) return 0;

    // Check if process is still running
    if (kill(info->daemon_pid, 0) == -1) {
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
        if (strstr(entry->d_name, "smart-cmd.") != entry->d_name) {
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