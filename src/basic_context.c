#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/utsname.h>
#include <glob.h>

/*
 * Basic Context Collector
 *
 * This file provides context collection for BASIC mode (non-daemon mode).
 * When daemon mode is disabled or unavailable, we need to collect context from
 * the user's actual shell environment, including bash history and system info.
 */

static int get_user_info(session_context_t *ctx) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return -1;

    snprintf(ctx->user.username, sizeof(ctx->user.username), "%s", pw->pw_name);

    // Get hostname
    if (gethostname(ctx->user.hostname, sizeof(ctx->user.hostname) - 1) == -1) {
        strcpy(ctx->user.hostname, "localhost");
    }

    return 0;
}

static int get_current_directory(session_context_t *ctx) {
    if (getcwd(ctx->user.cwd, sizeof(ctx->user.cwd) - 1) == NULL) {
        strcpy(ctx->user.cwd, "/unknown");
        return -1;
    }
    return 0;
}

// Simplified list of sensitive keywords to filter out
static const char *sensitive_keywords[] = {
    "password", "passwd", "pass", "pwd",
    "secret", "key", "token", "api_key",
    "auth", "login", "credential", "credentials",
    ".env", ".pem", ".key", ".p12", ".pfx",
    "sudo", "su ", "root",
    NULL
};

static int is_sensitive_command(const char *command) {
    if (!command || strlen(command) == 0) return 0;

    char lower_cmd[MAX_INPUT_LEN];
    strncpy(lower_cmd, command, sizeof(lower_cmd) - 1);
    lower_cmd[sizeof(lower_cmd) - 1] = '\0';

    // Convert to lowercase for comparison
    for (int i = 0; lower_cmd[i]; i++) {
        lower_cmd[i] = tolower(lower_cmd[i]);
    }

    // Check for sensitive keywords
    for (int i = 0; sensitive_keywords[i]; i++) {
        if (strstr(lower_cmd, sensitive_keywords[i]) != NULL) {
            return 1; // Sensitive command found
        }
    }

    return 0; // Not sensitive
}

static int get_command_history(session_context_t *ctx) {
    const char *history_file = getenv("HISTFILE");
    if (!history_file) {
        history_file = "~/.bash_history";
    }

    // Expand ~ to home directory
    wordexp_t exp_result;
    if (wordexp(history_file, &exp_result, 0) == 0) {
        FILE *fp = fopen(exp_result.we_wordv[0], "r");
        wordfree(&exp_result);

        if (fp) {
            char line[MAX_INPUT_LEN];
            char recent_commands[3][MAX_INPUT_LEN] = {0};
            int found = 0;

            // Simple approach: read all lines and keep last 3 non-sensitive
            while (fgets(line, sizeof(line), fp) && found < 3) {
                line[strcspn(line, "\n")] = 0; // Remove newline

                if (strlen(line) > 0 && !is_sensitive_command(line)) {
                    snprintf(recent_commands[found], sizeof(recent_commands[found]), "%s", line);
                    found++;
                }
            }

            // Store commands in context (reverse order - most recent first)
            if (found > 0) {
                // Most recent command
                snprintf(ctx->last_command, sizeof(ctx->last_command), "%s", recent_commands[found - 1]);

                // Add to terminal buffer for context
                snprintf(ctx->terminal_buffer, sizeof(ctx->terminal_buffer), "Recent history: ");
                for (int i = found - 1; i >= 0; i--) {
                    size_t buf_len = strlen(ctx->terminal_buffer);
                    size_t remaining = sizeof(ctx->terminal_buffer) - buf_len - 1;

                    if (remaining > 0) {
                        snprintf(ctx->terminal_buffer + buf_len, remaining,
                                "%s%s", recent_commands[i], (i > 0) ? "; " : "");
                    }
                }
            }

            fclose(fp);
        }
    }

    return 0;
}

static int detect_tmux(session_context_t *ctx) {
    const char *tmux = getenv("TMUX");
    if (tmux && strlen(tmux) > 0) {
        // In tmux session - simplified detection
        size_t buf_len = strlen(ctx->terminal_buffer);
        size_t remaining = sizeof(ctx->terminal_buffer) - buf_len - 1;

        if (remaining > 0) {
            snprintf(ctx->terminal_buffer + buf_len, remaining, "[tmux session] ");
        }
        return 1;
    }
    return 0;
}

static int detect_screen(session_context_t *ctx) {
    const char *stty = getenv("STY");
    if (stty && strlen(stty) > 0) {
        // In screen session
        size_t buf_len = strlen(ctx->terminal_buffer);
        size_t remaining = sizeof(ctx->terminal_buffer) - buf_len - 1;

        if (remaining > 0) {
            snprintf(ctx->terminal_buffer + buf_len, remaining, "[screen session] ");
        }
        return 1;
    }
    return 0;
}

static int get_environment_info(session_context_t *ctx) {
    // Get essential environment variables for context
    const char *env_vars[] = {"PWD", "USER", "HOME", "LANG", NULL};

    for (int i = 0; env_vars[i]; i++) {
        const char *value = getenv(env_vars[i]);
        if (value) {
            size_t buf_len = strlen(ctx->terminal_buffer);
            size_t remaining = sizeof(ctx->terminal_buffer) - buf_len - 1;

            if (remaining > 0) {
                snprintf(ctx->terminal_buffer + buf_len, remaining, "%s=%s ", env_vars[i], value);
            }
        }
    }

    return 0;
}

static int get_git_info(session_context_t *ctx) {
    // Simplified git detection and info collection
    if (system("git rev-parse --git-dir > /dev/null 2>&1") == 0) {
        FILE *fp;

        // Get current branch only
        fp = popen("git branch --show-current 2>/dev/null", "r");
        if (fp) {
            char branch[128];
            if (fgets(branch, sizeof(branch), fp)) {
                branch[strcspn(branch, "\n")] = 0;
                size_t buf_len = strlen(ctx->terminal_buffer);
                size_t remaining = sizeof(ctx->terminal_buffer) - buf_len - 1;

                if (remaining > 0) {
                    snprintf(ctx->terminal_buffer + buf_len, remaining, "[git:%s] ", branch);
                }
            }
            pclose(fp);
        }
    }

    return 0;
}

int collect_context(session_context_t *ctx) {
    if (!ctx) return -1;

    // Initialize context
    memset(ctx, 0, sizeof(session_context_t));

    // Collect basic information
    get_user_info(ctx);
    get_current_directory(ctx);
    get_command_history(ctx);

    // Detect multiplexer environment (simplified)
    if (!detect_tmux(ctx)) {
        detect_screen(ctx);
    }

    get_environment_info(ctx);
    get_git_info(ctx);

    return 0;
}