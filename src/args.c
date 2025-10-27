#define _GNU_SOURCE
#include "smart_cmd.h"
#include <string.h>

char *concat_remaining_args(int argc, char *argv[], int start_index) {
    if (start_index >= argc) return NULL;

    size_t total_len = 0;
    for (int i = start_index; i < argc; i++) {
        total_len += strlen(argv[i]) + 1;
    }

    char *result = malloc(total_len);
    if (!result) return NULL;

    result[0] = '\0';
    for (int i = start_index; i < argc; i++) {
        strcat(result, argv[i]);
        if (i < argc - 1) {
            strcat(result, " ");
        }
    }

    return result;
}

int parse_command_args(int argc, char *argv[], command_args_t *args) {
    if (!args) return -1;

    memset(args, 0, sizeof(command_args_t));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            args->show_help = 1;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            args->show_version = 1;
        } else if (strcmp(argv[i], "--test") == 0 || strcmp(argv[i], "-t") == 0) {
            args->run_tests = 1;
        } else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            args->show_config = 1;
        } else if (strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "-i") == 0) {
            if (i + 1 < argc) {
                args->input = argv[++i];
            }
        } else if (strcmp(argv[i], "--context") == 0 || strcmp(argv[i], "-x") == 0) {
            if (i + 1 < argc) {
                args->context = argv[++i];
            }
        } else if (argv[i][0] != '-') {
            if (!args->command) {
                args->command = argv[i];
            } else if (!args->input) {
                args->input = argv[i];
            }
        }
    }

    return 0;
}