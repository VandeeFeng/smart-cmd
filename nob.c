#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h/nob.h"

#define VERSION "1.0.0"

const char *core_sources[] = {
    "src/config.c",
    "src/llm_client.c",
    "src/basic_context.c",
    "src/pty_proxy.c",
    "src/daemon.c",
    "src/ipc.c",
    "src/daemon_history.c",
    "src/manager.c",
    "src/completion.c",
    "src/utils.c"
};

typedef struct {
    const char *output;
    const char *main_source;
    const char *compile_flag;
} Build_Target;

Build_Target targets[] = {
    {"smart-cmd", "src/main.c", ""},
    {"smart-cmd-completion", "src/completion.c", "-DCOMPLETION_BINARY"},
    {"smart-cmd-daemon", "src/smart_cmd_daemon.c", "-DDAEMON_BINARY"}
};

bool build_target(Build_Target target)
{
    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-std=c99", "-O2", "-DVERSION=" "\"" VERSION "\"");

    if (target.compile_flag && strlen(target.compile_flag) > 0) {
        nob_cmd_append(&cmd, target.compile_flag);
    }

    nob_cc_output(&cmd, target.output);

    nob_cmd_append(&cmd, target.main_source);

    for (size_t i = 0; i < ARRAY_LEN(core_sources); i++) {
        if (strcmp(target.main_source, core_sources[i]) != 0) {
            nob_cmd_append(&cmd, core_sources[i]);
        }
    }

    nob_cmd_append(&cmd, "-lutil", "-lcurl", "-ljson-c");

    const char **input_paths = NULL;
    size_t input_count = 0;

    input_paths = temp_alloc(sizeof(const char*) * (ARRAY_LEN(core_sources) + 1));
    input_paths[0] = target.main_source;
    input_count = 1;

    for (size_t i = 0; i < ARRAY_LEN(core_sources); i++) {
        if (strcmp(target.main_source, core_sources[i]) != 0) {
            input_paths[input_count++] = core_sources[i];
        }
    }

    if (nob_needs_rebuild(target.output, input_paths, input_count) > 0) {
        nob_log(INFO, "Building %s...", target.output);
        if (!nob_cmd_run(&cmd)) return false;
        nob_log(INFO, "Built %s successfully", target.output);
    } else {
        nob_log(INFO, "%s is up to date", target.output);
    }

    return true;
}

bool build_all(void)
{
    for (size_t i = 0; i < ARRAY_LEN(targets); i++) {
        if (!build_target(targets[i])) {
            return false;
        }
    }
    return true;
}

bool clean(void)
{
    bool result = true;

    for (size_t i = 0; i < ARRAY_LEN(targets); i++) {
        if (nob_file_exists(targets[i].output) == 1) {
            if (!nob_delete_file(targets[i].output)) {
                nob_log(ERROR, "Failed to delete %s", targets[i].output);
                result = false;
            }
        }
    }

    // .old files are automatically deleted by NOB_EXPERIMENTAL_DELETE_OLD

    const char *known_temp_files[] = {
        "nob.o",
        "nob.exe",
        "temp*",
        "*.tmp"
    };

    for (size_t i = 0; i < ARRAY_LEN(known_temp_files); i++) {
        if (strchr(known_temp_files[i], '*') == NULL && nob_file_exists(known_temp_files[i]) == 1) {
            if (!nob_delete_file(known_temp_files[i])) {
                nob_log(WARNING, "Failed to delete temp file %s", known_temp_files[i]);
            }
        }
    }

    nob_log(INFO, "Clean completed");
    return result;
}

bool install(void)
{
    if (!build_all()) return false;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "mkdir", "-p", nob_temp_sprintf("%s/.local/bin", getenv("HOME")));
    if (!nob_cmd_run(&cmd)) return false;

    for (size_t i = 0; i < ARRAY_LEN(targets); i++) {
        cmd.count = 0;
        nob_cmd_append(&cmd, "install", "-m", "755", targets[i].output,
                       nob_temp_sprintf("%s/.local/bin/%s", getenv("HOME"), targets[i].output));
        if (!nob_cmd_run(&cmd)) return false;
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "mkdir", "-p", nob_temp_sprintf("%s/.config/smart-cmd", getenv("HOME")));
    if (!nob_cmd_run(&cmd)) return false;

    cmd.count = 0;
    nob_cmd_append(&cmd, "install", "-m", "644", "smart-cmd.bash",
                   nob_temp_sprintf("%s/.config/smart-cmd/smart-cmd.bash", getenv("HOME")));
    if (!nob_cmd_run(&cmd)) return false;

    printf("Installation complete!\n");
    printf("Add the following to your ~/.bashrc:\n");
    printf("source ~/.config/smart-cmd/smart-cmd.bash\n");

    return true;
}

bool test(void)
{
    if (!build_all()) return false;

    Nob_Cmd cmd = {0};

    printf("Testing smart-cmd main commands...\n");
    nob_cmd_append(&cmd, "./smart-cmd", "status");
    if (!nob_cmd_run(&cmd)) {
        printf("WARNING: status test failed\n");
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "./smart-cmd", "mode");
    if (!nob_cmd_run(&cmd)) {
        printf("WARNING: mode test failed\n");
    }

    printf("Testing smart-cmd-completion...\n");
    cmd.count = 0;
    nob_cmd_append(&cmd, "echo", "docker -p", "|", "./smart-cmd-completion",
                   "--input", "docker -p", "--context",
                   "'{\"command_line\":\"docker -p\",\"cwd\":\"/tmp\",\"user\":\"test\",\"host\":\"test\"}'");
    if (!nob_cmd_run(&cmd)) {
        printf("WARNING: completion test failed\n");
    }

    printf("Testing smart-cmd-daemon...\n");
    cmd.count = 0;
    nob_cmd_append(&cmd, "./smart-cmd-daemon", "--status");
    if (!nob_cmd_run(&cmd)) {
        printf("WARNING: daemon status test failed\n");
    }

    printf("Testing new management commands...\n");
    cmd.count = 0;
    nob_cmd_append(&cmd, "./smart-cmd", "toggle");
    if (!nob_cmd_run(&cmd)) {
        printf("WARNING: toggle test failed\n");
    }

    return true;
}

void print_usage(const char *program_name)
{
    printf("Usage: %s [command]\n", program_name);
    printf("Commands:\n");
    printf("  (none)     Build all targets\n");
    printf("  clean      Remove built files\n");
    printf("  install    Build and install to ~/.local/bin\n");
    printf("  test       Run tests\n");
    printf("  help       Show this help message\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program_name = nob_shift(argv, argc);
    const char *command = NULL;

    if (argc > 0) {
        command = nob_shift(argv, argc);
    } else {
        command = NULL;
    }

    if (command == NULL || strcmp(command, "build") == 0) {
        if (!build_all()) return 1;
    } else if (strcmp(command, "clean") == 0) {
        if (!clean()) return 1;
    } else if (strcmp(command, "install") == 0) {
        if (!install()) return 1;
    } else if (strcmp(command, "test") == 0) {
        if (!test()) return 1;
    } else if (strcmp(command, "help") == 0 || strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage(program_name);
    } else {
        printf("ERROR: Unknown command: %s\n", command);
        print_usage(program_name);
        return 1;
    }

    return 0;
}
