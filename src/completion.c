#define _GNU_SOURCE
#include "smart_cmd.h"
#include <getopt.h>

#ifdef COMPLETION_BINARY

typedef struct {
    char input[MAX_INPUT_LEN];
    char cwd[512];
    char username[64];
    char hostname[256];
    char git_branch[128];
    int git_dirty;
} completion_context_t;

static void print_completion_usage(const char *program_name) {
    printf("Usage: %s\n", program_name);
    printf("Smart Command Completion Backend\n\n");
    printf("Reads command input from stdin.\n");
    printf("First line: command to complete\n");
    printf("Second line (optional): JSON context\n\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -v, --version        Show version information\n");
}

static void print_completion_version() {
    printf("smart-cmd-completion %s\n", VERSION);
}

static int parse_context_json(const char *context_json, completion_context_t *ctx) {
    // Simple JSON parsing for the context

    // Initialize defaults
    memset(ctx, 0, sizeof(completion_context_t));
    getcwd(ctx->cwd, sizeof(ctx->cwd) - 1);

    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        strncpy(ctx->username, pw->pw_name, sizeof(ctx->username) - 1);
    }
    gethostname(ctx->hostname, sizeof(ctx->hostname) - 1);

    // Parse JSON (basic string extraction)
    if (context_json) {
        char *json_copy = strdup(context_json);
        if (!json_copy) return -1;

        // Extract command_line
        char *cmd_start = strstr(json_copy, "\"command_line\":");
        if (cmd_start) {
            cmd_start = strchr(cmd_start, '"');
            if (cmd_start) cmd_start = strchr(cmd_start + 1, '"');
            if (cmd_start) {
                cmd_start++;
                char *cmd_end = strchr(cmd_start, '"');
                if (cmd_end) {
                    *cmd_end = '\0';
                    strncpy(ctx->input, cmd_start, sizeof(ctx->input) - 1);
                }
            }
        }

        // Extract cwd
        char *cwd_start = strstr(json_copy, "\"cwd\":");
        if (cwd_start) {
            cwd_start = strchr(cwd_start, '"');
            if (cwd_start) cwd_start = strchr(cwd_start + 1, '"');
            if (cwd_start) {
                cwd_start++;
                char *cwd_end = strchr(cwd_start, '"');
                if (cwd_end) {
                    *cwd_end = '\0';
                    strncpy(ctx->cwd, cwd_start, sizeof(ctx->cwd) - 1);
                }
            }
        }

        // Extract git info if present
        char *git_start = strstr(json_copy, "\"git\":");
        if (git_start) {
            // Extract branch
            char *branch_start = strstr(git_start, "\"branch\":");
            if (branch_start) {
                branch_start = strchr(branch_start, '"');
                if (branch_start) branch_start = strchr(branch_start + 1, '"');
                if (branch_start) {
                    branch_start++;
                    char *branch_end = strchr(branch_start, '"');
                    if (branch_end) {
                        *branch_end = '\0';
                        strncpy(ctx->git_branch, branch_start, sizeof(ctx->git_branch) - 1);
                    }
                }
            }

            // Extract dirty status
            ctx->git_dirty = (strstr(git_start, "\"dirty\":true") != NULL);
        }

        free(json_copy);
    }

    return 0;
}

static int get_multiple_suggestions(const char *input, const completion_context_t *ctx,
                                    const config_t *config, suggestion_t *suggestions, int max_suggestions) {
    if (!input || !ctx || !config || !suggestions || max_suggestions <= 0) return -1;

    int count = 0;

    // First suggestion
    if (send_to_llm(input, (const session_context_t*)ctx, config, &suggestions[count]) == 0) {
        count++;
    }

    // Second suggestion
    if (count < max_suggestions) {
        // For now, skip to avoid complexity
    }

    return count;
}


static void print_suggestions_plain(suggestion_t *suggestions, int count) {
    if (count > 0) {
        printf("%c%s", suggestions[0].type, suggestions[0].suggestion);
    }
}

int main(int argc, char *argv[]) {
    char input[MAX_INPUT_LEN] = {0};
    char context_json[MAX_CONTEXT_LEN] = {0};

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "hv", long_options, &option_index)) != -1) {
        switch (c) {
        case 'h':
            print_completion_usage(argv[0]);
            return 0;
        case 'v':
            print_completion_version();
            return 0;
        case '?':
            fprintf(stderr, "Unknown option. Use -h for help.\n");
            return 1;
        default:
            abort();
        }
    }

    // Read input from stdin
    if (isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: Input must be provided via stdin\n");
        return 1;
    }

    // Read first line - command to complete
    if (fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = '\0';
    }

    // Read second line - optional JSON context
    if (fgets(context_json, sizeof(context_json), stdin)) {
        context_json[strcspn(context_json, "\n")] = '\0';
    }

    // Load configuration
    config_t config;
    if (load_config(&config) == -1) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    // Parse context
    completion_context_t ctx;
    if (strlen(context_json) > 0) {
        if (parse_context_json(context_json, &ctx) == -1) {
            fprintf(stderr, "Failed to parse context JSON\n");
            return 1;
        }
    } else {
        // Use default context if no JSON provided
        if (parse_context_json(NULL, &ctx) == -1) {
            fprintf(stderr, "Failed to create default context\n");
            return 1;
        }
    }

    // Get suggestions
    suggestion_t suggestions[5];
    int suggestion_count = get_multiple_suggestions(input, &ctx, &config, suggestions, 5);

    if (suggestion_count > 0) {
        print_suggestions_plain(suggestions, suggestion_count);
    }

    return 0;
}

#endif // COMPLETION_BINARY

// Functions for main binary
#include <json-c/json.h>

int parse_completion_context(const char *context_json, session_context_t *ctx) {
    if (!context_json || !ctx) return -1;

    json_object *root = json_tokener_parse(context_json);
    if (!root) return -1;

    json_object *cwd, *user, *host;
    if (json_object_object_get_ex(root, "cwd", &cwd)) {
        strncpy(ctx->user.cwd, json_object_get_string(cwd), sizeof(ctx->user.cwd) - 1);
    }
    if (json_object_object_get_ex(root, "user", &user)) {
        strncpy(ctx->user.username, json_object_get_string(user), sizeof(ctx->user.username) - 1);
    }
    if (json_object_object_get_ex(root, "host", &host)) {
        strncpy(ctx->user.hostname, json_object_get_string(host), sizeof(ctx->user.hostname) - 1);
    }

    json_object_put(root);
    return 0;
}

int format_completion_output(const suggestion_t *suggestion, char **json_output) {
    if (!suggestion || !json_output) return -1;

    json_object *out_root = json_object_new_object();
    json_object *suggestions_arr = json_object_new_array();
    char full_suggestion[MAX_SUGGESTION_LEN + 2];
    snprintf(full_suggestion, sizeof(full_suggestion), "%c%s", suggestion->type, suggestion->suggestion);
    json_object_array_add(suggestions_arr, json_object_new_string(full_suggestion));
    json_object_object_add(out_root, "suggestions", suggestions_arr);

    const char *json_str = json_object_to_json_string(out_root);
    *json_output = strdup(json_str);

    json_object_put(out_root);
    return *json_output ? 0 : -1;
}

int run_basic_tests() {
    printf("Running basic functionality tests...\n");

    config_t config;
    printf("  Testing config loading... ");
    if (load_config(&config) == 0) {
        printf("OK\n");
    } else {
        printf("FAILED (no config found, using defaults)\n");
    }

    printf("  Testing context collection... ");
    session_context_t ctx;
    if (collect_context(&ctx) == 0) {
        printf("OK\n");
        printf("    Current directory: %s\n", ctx.user.cwd);
        printf("    User: %s\n", ctx.user.username);
        printf("    Host: %s\n", ctx.user.hostname);
    } else {
        printf("FAILED\n");
    }

    printf("Basic tests completed.\n");
    return 0;
}

int run_completion_mode(const char *input, const char *context_json) {
    if (!input) return -1;

    config_t config;
    load_config(&config);

    session_context_t ctx = {0};
    if (context_json) {
        if (parse_completion_context(context_json, &ctx) != 0) {
            fprintf(stderr, "error:Failed to parse context JSON.\n");
            return 1;
        }
    } else {
        if (collect_context(&ctx) != 0) {
            fprintf(stderr, "error:Failed to collect context.\n");
            return 1;
        }
    }

    suggestion_t suggestion;
    if (send_to_llm(input, &ctx, &config, &suggestion) == 0) {
        char *json_output;
        if (format_completion_output(&suggestion, &json_output) == 0) {
            printf("%s\n", json_output);
            free(json_output);
        } else {
            fprintf(stderr, "error:Failed to format output.\n");
            return 1;
        }
    } else {
        fprintf(stderr, "error:Failed to get LLM suggestion.\n");
        return 1;
    }

    return 0;
}
