#define _GNU_SOURCE
#include "smart_cmd.h"
#include <unistd.h>
#include <termios.h>

static int read_key_sequence(char *seq, size_t max_len) {
    for (size_t i = 0; i < max_len; i++) {
        if (read(STDIN_FILENO, &seq[i], 1) != 1) {
            return i;
        }
    }
    return max_len;
}

static int is_function_key_match(int expected_fn, const char *seq, size_t seq_len) {
    if (expected_fn < 1 || expected_fn > 12) return 0;

    // Handle F1-F4: OP, OQ, OR, OS format
    if (expected_fn >= 1 && expected_fn <= 4 && seq_len >= 2) {
        if (seq[0] == 'O') {
            char expected_char = 'P' + (expected_fn - 1);
            return seq[1] == expected_char;
        }
    }

    // Handle F5-F12: [15~, [17~, [18~, [19~, [20~, [21~, [23~, [24~ format
    if (expected_fn >= 5 && expected_fn <= 12 && seq_len >= 3) {
        if (seq[0] == '[' && seq[2] == '~') {
            const char *f5_to_f12_codes[] = {"15", "17", "18", "19", "20", "21", "23", "24"};
            const char *expected_code = f5_to_f12_codes[expected_fn - 5];
            return seq[1] == expected_code[0] && seq[2] == expected_code[1];
        }
    }

    return 0;
}

int check_trigger_key(const config_t *config, char first_char) {
    // Regular ASCII keys and Ctrl combinations
    if (config->trigger_key_value > 0) {
        return first_char == config->trigger_key_value;
    }

    // Function keys (negative values indicate function keys)
    if (config->trigger_key_value < 0) {
        int expected_fn = -(config->trigger_key_value + 100);
        if (expected_fn >= 1 && expected_fn <= 12 && first_char == 27) {
            char seq[3];
            int seq_len = read_key_sequence(seq, sizeof(seq));
            return is_function_key_match(expected_fn, seq, seq_len);
        }
    }

    return 0;
}

int handle_escape_sequence(void) {
    char seq[3];
    int seq_len = read_key_sequence(seq, sizeof(seq));

    if (seq_len >= 2 && seq[0] == '[') {
        switch (seq[1]) {
        case 'A': // Up arrow
            return -1;
        case 'B': // Down arrow
            return -1;
        case 'C': // Right arrow - accept suggestion
            return 2;
        case 'D': // Left arrow
            return -1;
        }
    }

    return -1;
}
