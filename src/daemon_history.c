#define _GNU_SOURCE
#include "smart_cmd.h"
#include <time.h>

/*
 * Daemon History Manager
 *
 * This file provides command history management for DAEMON mode.
 * When daemon mode is enabled, commands are tracked in an isolated PTY environment
 * with 1-hour retention and maximum 50 commands for privacy and efficiency.
 */

int init_command_history(command_history_manager_t *manager, const char *session_id) {
    if (!manager || !session_id) return -1;

    memset(manager, 0, sizeof(command_history_manager_t));

    // Setup history file path
    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";
    snprintf(manager->history_file, sizeof(manager->history_file),
            "%s/smart-cmd.history.%s", tmp_dir, session_id);

    // Load existing history if available
    load_command_history(manager);

    return 0;
}

void cleanup_command_history(command_history_manager_t *manager) {
    if (!manager) return;

    // Save history before cleanup
    save_command_history(manager);

    // Clear memory
    memset(manager, 0, sizeof(command_history_manager_t));
}

static int is_duplicate_command(const char *cmd1, const char *cmd2) {
    return strcmp(cmd1, cmd2) == 0;
}

int add_command_to_history(command_history_manager_t *manager, const char *command) {
    if (!manager || !command || strlen(command) == 0) return -1;
    if (strlen(command) >= MAX_INPUT_LEN) return -1;

    // Get current timestamp
    time_t now = time(NULL);

    // Remove old entries (older than 1 hour)
    time_t cutoff_time = now - 3600; // 1 hour ago

    // Clean old entries first
    int cleaned_count = 0;
    for (int i = 0; i < manager->count; i++) {
        if (manager->commands[i].timestamp < cutoff_time) {
            cleaned_count++;
        } else {
            // Move command forward if there were cleaned entries before it
            if (cleaned_count > 0) {
                manager->commands[i - cleaned_count] = manager->commands[i];
            }
        }
    }
    manager->count -= cleaned_count;

    // Check for duplicate with last command
    int last_index = (manager->current_index - 1 + 50) % 50;
    if (manager->count > 0 &&
        is_duplicate_command(command, manager->commands[last_index].command)) {
        return 0; // Skip duplicate
    }

    // Add new command using circular buffer
    int index = manager->current_index % MAX_HISTORY_COMMANDS;
    snprintf(manager->commands[index].command, MAX_INPUT_LEN, "%s", command);
    manager->commands[index].timestamp = now;

    if (manager->count < MAX_HISTORY_COMMANDS) {
        manager->count++;
    }
    manager->current_index = (manager->current_index + 1) % MAX_HISTORY_COMMANDS;

    return 0;
}

int get_recent_commands(command_history_manager_t *manager, char *recent_commands, int count, time_t max_age) {
    if (!manager || !recent_commands || count <= 0) return -1;

    time_t now = time(NULL);
    time_t cutoff_time = (max_age > 0) ? (now - max_age) : 0;

    recent_commands[0] = '\0';
    int found = 0;
    int commands_added = 0;

    // Start from most recent commands and work backwards
    for (int i = 0; i < manager->count && commands_added < count; i++) {
        int index = (manager->current_index - 1 - i + MAX_HISTORY_COMMANDS) % MAX_HISTORY_COMMANDS;

        if (manager->commands[index].command[0] != '\0' &&
            manager->commands[index].timestamp >= cutoff_time) {

            if (found > 0) {
                size_t buf_len = strlen(recent_commands);
                size_t remaining = MAX_CONTEXT_LEN - buf_len - 1;

                if (remaining > 0) {
                    snprintf(recent_commands + buf_len, remaining, ", %s", manager->commands[index].command);
                }
            } else {
                snprintf(recent_commands, MAX_CONTEXT_LEN, "%s", manager->commands[index].command);
            }

            found++;
            commands_added++;
        }
    }

    return found;
}

int save_command_history(command_history_manager_t *manager) {
    if (!manager || manager->count == 0) return 0;

    FILE *fp = fopen(manager->history_file, "w");
    if (!fp) return -1;

    // Write header with count
    fprintf(fp, "%d\n", manager->count);

    // Write each command with timestamp
    for (int i = 0; i < manager->count; i++) {
        int index = (manager->current_index - manager->count + i + MAX_HISTORY_COMMANDS) % MAX_HISTORY_COMMANDS;
        fprintf(fp, "%ld %s\n", manager->commands[index].timestamp,
                manager->commands[index].command);
    }

    fclose(fp);
    return 0;
}

int load_command_history(command_history_manager_t *manager) {
    if (!manager) return -1;

    FILE *fp = fopen(manager->history_file, "r");
    if (!fp) return 0; // No existing history is fine

    int count;
    if (fscanf(fp, "%d\n", &count) != 1) {
        fclose(fp);
        return -1;
    }

    // Limit the number of commands to load
    if (count > 50) count = 50;

    time_t now = time(NULL);
    time_t cutoff_time = now - 3600; // 1 hour ago

    manager->count = 0;
    manager->current_index = 0;

    char line[MAX_INPUT_LEN + 64]; // Extra space for timestamp
    while (fgets(line, sizeof(line), fp) && manager->count < count) {
        time_t timestamp;
        char command[MAX_INPUT_LEN];

        if (sscanf(line, "%ld %[^\\n]", &timestamp, command) == 2) {
            // Only load recent commands (within last hour)
            if (timestamp >= cutoff_time) {
                snprintf(manager->commands[manager->count].command, MAX_INPUT_LEN, "%s", command);
                manager->commands[manager->count].timestamp = timestamp;
                manager->count++;
            }
        }
    }

    manager->current_index = manager->count;
    fclose(fp);
    return manager->count;
}