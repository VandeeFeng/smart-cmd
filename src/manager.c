#define _GNU_SOURCE
#include "smart_cmd.h"
#include "defaults.h"
#include <glob.h>

int find_running_daemon(daemon_session_t *info) {
    RETURN_IF_NULL(info, -1);

    const char *tmp_dir = get_smart_cmd_tmpdir();
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s/%s.*", tmp_dir, LOCK_FILE_PREFIX);

    glob_t glob_result;
    int ret = glob(pattern, 0, NULL, &glob_result);
    if (ret != 0) {
        globfree(&glob_result);
        return -1;
    }

    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        const char *lock_file = glob_result.gl_pathv[i];

        FILE *f = fopen(lock_file, "r");
        if (f) {
            pid_t pid;
            if (fscanf(f, "%d", &pid) == 1 && is_process_running(pid)) {
                fclose(f);

                info->daemon_pid = pid;
                safe_string_copy(info->paths.lock_file, lock_file, sizeof(info->paths.lock_file));

                const char *filename = strrchr(lock_file, '/') + 1;
                const char *session_start = strrchr(filename, '.');
                if (session_start) {
                    safe_string_copy(info->paths.session_id, session_start + 1, sizeof(info->paths.session_id));

                    // Generate socket path
                    generate_socket_path(info->paths.socket_path, sizeof(info->paths.socket_path),
                                      info->paths.session_id);
                }

                info->active = 1;
                globfree(&glob_result);
                return 0;
            }
            fclose(f);
        }
    }

    globfree(&glob_result);
    return -1;
}

int cmd_toggle() {
    char *state_file = get_temp_file_path("state");
    if (!state_file) return -1;

    FILE *f = fopen(state_file, "r");
    if (f) {
        int enabled;
        if (fscanf(f, "%d", &enabled) == 1) {
            fclose(f);
            enabled = !enabled;
        } else {
            fclose(f);
            enabled = 1;
        }

        f = fopen(state_file, "w");
        if (f) {
            fprintf(f, "%d", enabled);
            fclose(f);
        }

        printf("%s\n", enabled ? MSG_COMPLETION_ENABLED : MSG_COMPLETION_DISABLED);
    } else {
        f = fopen(state_file, "w");
        if (f) {
            fprintf(f, "1");
            fclose(f);
        }
        printf("%s\n", MSG_COMPLETION_ENABLED);
    }

    free(state_file);
    return 0;
}

int cmd_status() {
    config_t config;
    load_config(&config);

    printf("Current mode: %s\n",
           config.enable_proxy_mode ? "DAEMON (PTY mode)" : "BASIC (direct AI)");

    if (config.enable_proxy_mode) {
        daemon_session_t info = {0};
        if (find_running_daemon(&info) == 0) {
            printf("Daemon is running (PID: %d)\n", info.daemon_pid);
            printf("Session: %s\n", info.paths.session_id);
            printf("Socket: %s\n", info.paths.socket_path);
            printf("Status: Running\n");
        } else {
            printf("Daemon is not running (will start on demand)\n");
        }
    } else {
        printf("Daemon mode is disabled in configuration\n");
    }

    return 0;
}

int cmd_start() {
    config_t config;
    if (load_config(&config) != 0 || !config.enable_proxy_mode) {
        fprintf(stderr, "Daemon mode is disabled in configuration\n");
        return 1;
    }

    char *daemon_bin = get_default_bin_path("smart-cmd-daemon");
    if (!daemon_bin) {
        fprintf(stderr, "Failed to get daemon binary path\n");
        return 1;
    }

    if (access(daemon_bin, X_OK) != 0) {
        fprintf(stderr, "Daemon binary not found or not executable: %s\n", daemon_bin);
        free(daemon_bin);
        return 1;
    }

    daemon_session_t info = {0};
    if (find_running_daemon(&info) == 0) {
        printf("Daemon already running (PID: %d)\n", info.daemon_pid);
        free(daemon_bin);
        return 0;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        free(daemon_bin);
        return 1;
    }

    if (pid == 0) {
        execl(daemon_bin, daemon_bin, NULL);
        perror("execl");
        exit(1);
    } else {
        free(daemon_bin);

        usleep(DEFAULT_DAEMON_STARTUP_DELAY);

        if (find_running_daemon(&info) == 0) {
            printf("%s (PID: %d, Session: %s)\n",
                   MSG_DAEMON_STARTED, info.daemon_pid, info.paths.session_id);
            return 0;
        } else {
            fprintf(stderr, "%s\n", MSG_DAEMON_START_FAILED);
            return 1;
        }
    }
}

int cmd_stop() {
    daemon_session_t info = {0};
    if (find_running_daemon(&info) != 0) {
        printf("Daemon is not running\n");
        return 0;
    }

    if (kill(info.daemon_pid, SIGTERM) == 0) {
        usleep(100000);
        cleanup_lock_file(info.paths.lock_file);
        unlink(info.paths.socket_path);
        printf("%s\n", MSG_DAEMON_STOPPED);
        return 0;
    } else {
        perror("kill");
        return 1;
    }
}

int cmd_mode() {
    config_t config;
    load_config(&config);

    printf("Smart-cmd configuration:\n");
    printf("  Mode: %s\n",
           config.enable_proxy_mode ? "DAEMON (PTY context + command history)" : "BASIC (direct AI completion)");

    char *config_path = get_config_file_path();
    printf("  Config file: %s\n", config_path);
    free(config_path);

    char *state_file = get_temp_file_path("state");
    FILE *f = fopen(state_file, "r");
    if (f) {
        int enabled;
        if (fscanf(f, "%d", &enabled) == 1) {
            printf("  Smart completion: %s\n", enabled ? "enabled" : "disabled");
        }
        fclose(f);
    } else {
        printf("  Smart completion: enabled\n");
    }
    free(state_file);

    if (config.enable_proxy_mode) {
        printf("\nDaemon features:\n");
        printf("  - PTY isolation for security\n");
        printf("  - Command history (last %d commands, %d seconds)\n",
               DEFAULT_HISTORY_LIMIT, DEFAULT_SESSION_TIMEOUT);
        printf("  - Context-aware AI suggestions\n");
        printf("  - Session persistence\n");
    } else {
        printf("\nBasic mode features:\n");
        printf("  - Direct AI completion\n");
        printf("  - Environment context (cwd, git, etc.)\n");
        printf("  - No persistent history\n");
        printf("  - Faster response time\n");
    }

    return 0;
}


void show_config() {
    config_t config;
    if (load_config(&config) == 0) {
        printf("Current Configuration:\n");
        printf("  LLM Provider: %s\n", config.llm.provider);
        printf("  Model: %s\n", config.llm.model);
        printf("  API Key: %s\n",
               config.llm.api_key[0] ? "***hidden***" : "not set");
        printf("  Endpoint: %s\n", config.llm.endpoint);
        printf("  Trigger Key: %s\n", config.trigger_key);
        printf("  Proxy Mode: %s\n", config.enable_proxy_mode ? "enabled" : "disabled");
    } else {
        printf("%s\n", MSG_CONFIG_NOT_FOUND);
    }
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [command]\n", program_name);
    printf("Smart Command Completion Utility\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -t, --test     Run basic functionality tests\n");
    printf("  -v, --version  Show version information\n");
    printf("  -c, --config   Show current configuration\n");
    printf("  -i, --input    Input command for completion\n");
    printf("  -x, --context  JSON context for completion\n");
    printf("\n");
    printf("Commands:\n");
    printf("  toggle         Enable/disable smart completion\n");
    printf("  status         Show daemon status\n");
    printf("  start          Manually start daemon\n");
    printf("  stop           Stop daemon\n");
    printf("  mode           Show current mode and configuration\n");
    printf("\n");
    printf("Working Modes:\n");
    printf("  Basic Mode:     Direct AI completion without persistent context\n");
    printf("  Daemon Mode:    PTY isolation with command history and context memory\n");
    printf("\n");
    printf("Configuration: ~/.config/smart-cmd/config.json\n");
    printf("  Set 'enable_proxy_mode' to true/false to switch modes\n");
    printf("\n");
    printf("For bash integration, source smart-cmd.bash in your ~/.bashrc.\n");
    printf("Use Ctrl+O to trigger AI completion in bash.\n");
}

void print_version() {
    printf("smart-cmd version %s\n", VERSION);
}


