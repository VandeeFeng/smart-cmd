#define _GNU_SOURCE
#include "smart_cmd.h"
#include "defaults.h"
#include <getopt.h>


typedef struct {
    const char *name;
    int (*handler)();
} command_t;

static const command_t commands[] = {
    {"toggle", cmd_toggle},
    {"status", cmd_status},
    {"start", cmd_start},
    {"stop", cmd_stop},
    {"mode", cmd_mode},
    {NULL, NULL}
};

static int handle_long_options(int opt, const char *program_name) {
    switch (opt) {
    case 'h':
        print_usage(program_name);
        return 0;
    case 'v':
        print_version();
        return 0;
    case 'c':
        show_config();
        return 0;
    case 't':
        return run_basic_tests();
    default:
        return 1;
    }
}

static int route_command(const char *command_name) {
    if (!command_name) return -1;

    for (int i = 0; commands[i].name; i++) {
        if (strcmp(command_name, commands[i].name) == 0) {
            return commands[i].handler();
        }
    }
    return -1;
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"test",    no_argument,       0, 't'},
        {"version", no_argument,       0, 'v'},
        {"config",  no_argument,       0, 'c'},
        {"input",   required_argument, 0, 'i'},
        {"context", required_argument, 0, 'x'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;
    char *input_str = NULL;
    char *context_str = NULL;

    while ((c = getopt_long(argc, argv, "htvci:x:", long_options, &option_index)) != -1) {
        if (c == 'i') {
            input_str = optarg;
        } else if (c == 'x') {
            context_str = optarg;
        } else if (c != '?') {
            int result = handle_long_options(c, argv[0]);
            if (result >= 0) return result;
        }
    }

    if (optind < argc) {
        const char *command = argv[optind];
        int result = route_command(command);
        if (result >= 0) return result;

        if (!input_str) {
            char *fallback_input = concat_remaining_args(argc, argv, optind);
            if (fallback_input) {
                result = run_completion_mode(fallback_input, NULL);
                free(fallback_input);
                return result;
            }
        }
    }

    if (input_str) {
        return run_completion_mode(input_str, context_str);
    }

    print_usage(argv[0]);
    return 0;
}