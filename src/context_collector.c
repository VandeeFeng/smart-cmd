#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/utsname.h>
#include <glob.h>

static int get_user_info(context_t *ctx) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return -1;

    strncpy(ctx->username, pw->pw_name, sizeof(ctx->username) - 1);

    // Get hostname
    if (gethostname(ctx->hostname, sizeof(ctx->hostname) - 1) == -1) {
        strcpy(ctx->hostname, "localhost");
    }

    return 0;
}

static int get_current_directory(context_t *ctx) {
    if (getcwd(ctx->cwd, sizeof(ctx->cwd) - 1) == NULL) {
        strcpy(ctx->cwd, "/unknown");
        return -1;
    }
    return 0;
}

static int get_command_history(context_t *ctx) {
    // This is a simplified version - in a real implementation,
    // you might want to monitor bash history in real-time
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
            // Read last few lines of history
            fseek(fp, -1024, SEEK_END); // Go to near end of file
            fgets(line, sizeof(line), fp); // Skip partial line

            int count = 0;
            while (fgets(line, sizeof(line), fp) && count < 10) {
                // Remove newline
                line[strcspn(line, "\n")] = 0;
                if (strlen(line) > 0) {
                    // Store last command (in a real implementation,
                    // you'd want to store multiple commands)
                    strncpy(ctx->last_command, line, sizeof(ctx->last_command) - 1);
                    count++;
                }
            }
            fclose(fp);
        }
    }

    return 0;
}

static int detect_tmux(context_t *ctx) {
    const char *tmux = getenv("TMUX");
    if (tmux && strlen(tmux) > 0) {
        // In tmux session
        char tmux_cmd[512];
        snprintf(tmux_cmd, sizeof(tmux_cmd),
                 "tmux display-message -p '#S:#I.#P' 2>/dev/null");

        FILE *fp = popen(tmux_cmd, "r");
        if (fp) {
            char session_info[256];
            if (fgets(session_info, sizeof(session_info), fp)) {
                session_info[strcspn(session_info, "\n")] = 0;
                // Append to terminal buffer as context
                strncat(ctx->terminal_buffer, "[tmux:",
                        sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
                strncat(ctx->terminal_buffer, session_info,
                        sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
                strncat(ctx->terminal_buffer, "] ",
                        sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
            }
            pclose(fp);
        }
        return 1;
    }
    return 0;
}

static int detect_screen(context_t *ctx) {
    const char *stty = getenv("STY");
    if (stty && strlen(stty) > 0) {
        // In screen session
        strncat(ctx->terminal_buffer, "[screen:",
                sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
        strncat(ctx->terminal_buffer, stty,
                sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
        strncat(ctx->terminal_buffer, "] ",
                sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
        return 1;
    }
    return 0;
}

static int get_environment_info(context_t *ctx) {
    // Get some relevant environment variables for context
    const char *env_vars[] = {"PWD", "USER", "HOME", "PATH", "LANG", NULL};

    for (int i = 0; env_vars[i]; i++) {
        const char *value = getenv(env_vars[i]);
        if (value) {
            char env_entry[512];
            snprintf(env_entry, sizeof(env_entry), "%s=%s\n", env_vars[i], value);
            strncat(ctx->terminal_buffer, env_entry,
                    sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
        }
    }

    return 0;
}

static int get_git_info(context_t *ctx) {
    // Check if we're in a git repository
    if (system("git rev-parse --git-dir > /dev/null 2>&1") == 0) {
        FILE *fp;

        // Get current branch
        fp = popen("git branch --show-current 2>/dev/null", "r");
        if (fp) {
            char branch[256];
            if (fgets(branch, sizeof(branch), fp)) {
                branch[strcspn(branch, "\n")] = 0;
                char git_info[512];
                snprintf(git_info, sizeof(git_info), "[git:%s] ", branch);
                strncat(ctx->terminal_buffer, git_info,
                        sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
            }
            pclose(fp);
        }

        // Get git status summary
        fp = popen("git status --porcelain 2>/dev/null | head -5", "r");
        if (fp) {
            char status_line[256];
            while (fgets(status_line, sizeof(status_line), fp)) {
                status_line[strcspn(status_line, "\n")] = 0;
                if (strlen(status_line) > 0) {
                    strncat(ctx->terminal_buffer, "[git:",
                            sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
                    strncat(ctx->terminal_buffer, status_line,
                            sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
                    strncat(ctx->terminal_buffer, "] ",
                            sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
                }
            }
            pclose(fp);
        }
    }

    return 0;
}

int collect_context(context_t *ctx, pty_proxy_t *proxy) {
    if (!ctx) return -1;

    // Initialize context
    memset(ctx, 0, sizeof(context_t));

    // Collect basic information
    get_user_info(ctx);
    get_current_directory(ctx);
    get_command_history(ctx);

    // Add terminal buffer from PTY proxy if available
    if (proxy) {
        char pty_context[MAX_CONTEXT_LEN / 2];
        if (get_pty_context(proxy, pty_context, sizeof(pty_context)) > 0) {
            strncat(ctx->terminal_buffer, pty_context,
                    sizeof(ctx->terminal_buffer) - strlen(ctx->terminal_buffer) - 1);
        }
    }

    // Detect multiplexer environment
    if (!detect_tmux(ctx)) {
        detect_screen(ctx);
    }

    get_environment_info(ctx);

    get_git_info(ctx);

    return 0;
}
