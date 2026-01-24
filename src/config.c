#define _GNU_SOURCE
#include "smart_cmd.h"
#include "defaults.h"
#include <json-c/json.h>
#include <wordexp.h>

static char* expand_path(const char* path) {
    wordexp_t exp_result;
    if (wordexp(path, &exp_result, 0) != 0) return strdup(path);
    char* expanded = strdup(exp_result.we_wordv[0]);
    wordfree(&exp_result);
    return expanded;
}

static int parse_keybinding(const char* key_str) {
    if (!key_str) return 15;

    // Handle ctrl+[a-z] combinations
    if (strncmp(key_str, "ctrl+", 5) == 0 && strlen(key_str) == 6) {
        char key = tolower(key_str[5]);
        if (key >= 'a' && key <= 'z') return 1 + (key - 'a');
    }

    // Handle function keys f1-f12
    if (key_str[0] == 'f' && strlen(key_str) >= 2 && strlen(key_str) <= 3) {
        int fn = atoi(key_str + 1);
        if (fn >= 1 && fn <= 12) return -(100 + fn);
    }

    // Handle special keys
    const char* specials[] = {"escape", "enter", "tab", "space", "backspace"};
    const int codes[] = {27, 13, 9, 32, 127};
    for (int i = 0; i < 5; i++) {
        if (strcmp(key_str, specials[i]) == 0) return codes[i];
    }

    // Handle single character keys
    if (strlen(key_str) == 1) return (unsigned char)key_str[0];

    return 15;
}

static void load_json_str(json_object *obj, const char *key, char *dest, size_t size) {
    json_object *tmp;
    if (json_object_object_get_ex(obj, key, &tmp)) {
        snprintf(dest, size, "%s", json_object_get_string(tmp));
    }
}

static void load_provider_config(json_object* root, config_t* config) {
    char selected_provider[64] = "openai";
    json_object* llm_obj;

    if (json_object_object_get_ex(root, "llm", &llm_obj)) {
        load_json_str(llm_obj, "provider", selected_provider, sizeof(selected_provider));
    }
    snprintf(config->llm.provider, sizeof(config->llm.provider), "%s", selected_provider);

    json_object* providers_obj;
    json_object* provider_config;

    if (json_object_object_get_ex(root, "providers", &providers_obj) &&
        json_object_object_get_ex(providers_obj, selected_provider, &provider_config)) {
        load_json_str(provider_config, "model", config->llm.model, sizeof(config->llm.model));
        load_json_str(provider_config, "endpoint", config->llm.endpoint, sizeof(config->llm.endpoint));
    }

    if (json_object_object_get_ex(root, "llm", &llm_obj)) {
        load_json_str(llm_obj, "model", config->llm.model, sizeof(config->llm.model));
        load_json_str(llm_obj, "endpoint", config->llm.endpoint, sizeof(config->llm.endpoint));
        load_json_str(llm_obj, "api_key", config->llm.api_key, sizeof(config->llm.api_key));
    }
}

static void load_api_key_from_env(config_t* config) {
    const char* env_api_key = NULL;

    if (strcmp(config->llm.provider, "openai") == 0) {
        env_api_key = getenv("OPENAI_API_KEY");
    } else if (strcmp(config->llm.provider, "gemini") == 0) {
        env_api_key = getenv("GEMINI_API_KEY");
    } else if (strcmp(config->llm.provider, "openrouter") == 0) {
        env_api_key = getenv("OPENROUTER_API_KEY");
    }

    if (env_api_key && strlen(env_api_key) > 0) {
        snprintf(config->llm.api_key, sizeof(config->llm.api_key), "%s", env_api_key);
    }
}

int load_config(config_t *config) {
    if (!config) return -1;

    strcpy(config->llm.provider, "openai");
    strcpy(config->llm.api_key, "");
    strcpy(config->llm.model, "gpt-4.1-nano");
    strcpy(config->llm.endpoint, DEFAULT_OPENAI_ENDPOINT);
    strcpy(config->trigger_key, "ctrl+o");
    config->trigger_key_value = parse_keybinding("ctrl+o");
    config->enable_proxy_mode = 1;
    config->show_startup_messages = 1;

    char *config_path = expand_path(CONFIG_FILE_PATH);
    FILE *fp = fopen(config_path, "r");
    free(config_path);
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer) { fclose(fp); return -1; }

    fread(buffer, 1, file_size, fp);
    buffer[file_size] = '\0';
    fclose(fp);

    json_object *root = json_tokener_parse(buffer);
    free(buffer);
    if (!root) return -1;

    load_provider_config(root, config);
    load_api_key_from_env(config);

    json_object *trigger_obj;
    if (json_object_object_get_ex(root, "trigger_key", &trigger_obj)) {
        const char *trigger_str = json_object_get_string(trigger_obj);
        snprintf(config->trigger_key, sizeof(config->trigger_key), "%s", trigger_str);
        config->trigger_key_value = parse_keybinding(trigger_str);
    }

    json_object *proxy_obj, *startup_obj;
    if (json_object_object_get_ex(root, "enable_proxy_mode", &proxy_obj))
        config->enable_proxy_mode = json_object_get_boolean(proxy_obj);
    if (json_object_object_get_ex(root, "show_startup_messages", &startup_obj))
        config->show_startup_messages = json_object_get_boolean(startup_obj);

    json_object_put(root);
    return 0;
}

char* get_default_bin_path(const char* binary_name) {
    if (!binary_name) return NULL;
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char* path = malloc(strlen(home) + strlen("/.local/bin/") + strlen(binary_name) + 1);
    if (!path) return NULL;

    snprintf(path, strlen(home) + strlen("/.local/bin/") + strlen(binary_name) + 1,
             "%s/.local/bin/%s", home, binary_name);
    return path;
}

char* get_config_file_path(void) {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char* path = malloc(strlen(home) + strlen("/.config/smart-cmd/config.json") + 1);
    if (!path) return NULL;

    snprintf(path, strlen(home) + strlen("/.config/smart-cmd/config.json") + 1,
             "%s/.config/smart-cmd/config.json", home);
    return path;
}

int get_temp_file_path(char* path, size_t path_size, const char* prefix) {
    if (!path || !prefix) return -1;

    char session_id[MAX_SESSION_ID];
    if (generate_session_id(session_id, sizeof(session_id)) != 0) return -1;
    return generate_temp_file_path(path, path_size, prefix, session_id);
}
