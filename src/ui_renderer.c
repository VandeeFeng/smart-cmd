#define _GNU_SOURCE
#include "smart_cmd.h"
#include <sys/ioctl.h>

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_GRAY    "\033[90m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"

// Control sequences
#define CLEAR_LINE    "\033[K"
#define SAVE_CURSOR   "\033[s"
#define RESTORE_CURSOR "\033[u"

static int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

static void clear_suggestion_area() {
    write(STDOUT_FILENO, CLEAR_LINE, strlen(CLEAR_LINE));
}

int render_suggestion(const suggestion_t *suggestion, const char *current_input) {
    if (!suggestion || !current_input) return -1;

    if (!suggestion->visible || strlen(suggestion->suggestion) == 0) {
        clear_suggestion_area();
        return 0;
    }

    write(STDOUT_FILENO, SAVE_CURSOR, strlen(SAVE_CURSOR));

    write(STDOUT_FILENO, CLEAR_LINE, strlen(CLEAR_LINE));

    // Redraw current input
    write(STDOUT_FILENO, "$ ", 2);
    write(STDOUT_FILENO, current_input, strlen(current_input));

    // Calculate available space for suggestion
    int terminal_width = get_terminal_width();
    int prompt_and_input_len = 2 + strlen(current_input);
    int available_space = terminal_width - prompt_and_input_len;

    if (available_space > 2) {
        write(STDOUT_FILENO, COLOR_GRAY, strlen(COLOR_GRAY));

        // Truncate suggestion if needed
        char truncated_suggestion[MAX_SUGGESTION_LEN];
        strncpy(truncated_suggestion, suggestion->suggestion, sizeof(truncated_suggestion) - 1);
        truncated_suggestion[sizeof(truncated_suggestion) - 1] = '\0';

        if (strlen(truncated_suggestion) > available_space - 1) {
            truncated_suggestion[available_space - 2] = '\0';
        }

        // Add indicator for completion type
        char indicator[2] = {suggestion->type, '\0'};
        write(STDOUT_FILENO, indicator, 1);
        write(STDOUT_FILENO, truncated_suggestion, strlen(truncated_suggestion));

        write(STDOUT_FILENO, COLOR_RESET, strlen(COLOR_RESET));
    }

    write(STDOUT_FILENO, RESTORE_CURSOR, strlen(RESTORE_CURSOR));

    return 0;
}

int show_completion_indicator(int show) {
    if (show) {
        // Show a subtle indicator that completion is being processed
        write(STDOUT_FILENO, COLOR_GRAY, strlen(COLOR_GRAY));
        write(STDOUT_FILENO, " [thinking...]", 13);
        write(STDOUT_FILENO, COLOR_RESET, strlen(COLOR_RESET));
    } else {
        // Clear the thinking indicator
        write(STDOUT_FILENO, "\r", 1);
        write(STDOUT_FILENO, CLEAR_LINE, strlen(CLEAR_LINE));
    }

    return 0;
}

int display_help() {
    printf("\n" COLOR_GREEN "Smart Command Completion Help" COLOR_RESET "\n");
    printf("=====================================\n");
    printf(COLOR_GRAY "Ctrl+O" COLOR_RESET " - Trigger AI completion\n");
    printf(COLOR_GRAY "Right Arrow" COLOR_RESET " - Accept suggested completion\n");
    printf(COLOR_GRAY "ESC/Ctrl+C" COLOR_RESET " - Exit the program\n");
    printf("\nCompletion types:\n");
    printf(COLOR_GREEN "+" COLOR_RESET " - Command completion\n");
    printf(COLOR_GREEN "=" COLOR_RESET " - New command suggestion\n");
    printf("\n");

    return 0;
}

int display_error(const char *message) {
    write(STDOUT_FILENO, COLOR_RED, strlen(COLOR_RED));
    write(STDOUT_FILENO, "Error: ", 7);
    write(STDOUT_FILENO, message, strlen(message));
    write(STDOUT_FILENO, COLOR_RESET, strlen(COLOR_RESET));
    write(STDOUT_FILENO, "\n", 1);

    return 0;
}

int accept_suggestion(char *current_input, const suggestion_t *suggestion) {
    if (!current_input || !suggestion || !suggestion->visible) return -1;

    if (suggestion->type == '+') {
        // Append completion to current input
        size_t current_len = strlen(current_input);
        size_t suggestion_len = strlen(suggestion->suggestion);

        if (current_len + suggestion_len < MAX_INPUT_LEN - 1) {
            strcat(current_input, suggestion->suggestion);

            // Clear and redraw the line
            write(STDOUT_FILENO, "\r", 1);
            write(STDOUT_FILENO, CLEAR_LINE, strlen(CLEAR_LINE));
            write(STDOUT_FILENO, "$ ", 2);
            write(STDOUT_FILENO, current_input, strlen(current_input));
        }
    } else if (suggestion->type == '=') {
        // Replace current input with new command
        strncpy(current_input, suggestion->suggestion, MAX_INPUT_LEN - 1);
        current_input[MAX_INPUT_LEN - 1] = '\0';

        // Clear and redraw the line
        write(STDOUT_FILENO, "\r", 1);
        write(STDOUT_FILENO, CLEAR_LINE, strlen(CLEAR_LINE));
        write(STDOUT_FILENO, "$ ", 2);
        write(STDOUT_FILENO, current_input, strlen(current_input));
    }

    return 0;
}
