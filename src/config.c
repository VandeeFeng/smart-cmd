#define _GNU_SOURCE
#include "smart_cmd.h"
#include "defaults.h"
#include <json-c/json.h>
#include <wordexp.h>

static char* expand_path(const char* path) {
    wordexp_t exp_result;
    if (wordexp(path, &exp_result, 0) == 0) {
        char* expanded = strdup(exp_result.we_wordv[0]);
        wordfree(&exp_result);
        return expanded;
    }
    return strdup(path);
}

static int parse_keybinding(const char* key_str) {
    if (!key_str) return 15; // Default to Ctrl+O (ASCII 15)

    // Handle ctrl+[a-z] combinations
    if (strncmp(key_str, "ctrl+", 5) == 0 && strlen(key_str) == 6) {
        char key = tolower(key_str[5]);
        if (key >= 'a' && key <= 'z') {
            return 1 + (key - 'a'); // Ctrl+A = 1, Ctrl+B = 2, ..., Ctrl+Z = 26
        }
    }

    // Handle function keys f1-f12
    if (strncmp(key_str, "f", 1) == 0 && strlen(key_str) >= 2 && strlen(key_str) <= 3) {
        int fn = atoi(key_str + 1);
        if (fn >= 1 && fn <= 12) {
            // Return negative values to indicate function keys
            return -(100 + fn); // f1 = -101, f2 = -102, ..., f12 = -112
        }
    }

    // Handle special keys
    if (strcmp(key_str, "escape") == 0) return 27;
    if (strcmp(key_str, "enter") == 0) return 13;
    if (strcmp(key_str, "tab") == 0) return 9;
    if (strcmp(key_str, "space") == 0) return 32;
    if (strcmp(key_str, "backspace") == 0) return 127;

    // Handle single character keys
    if (strlen(key_str) == 1) {
        return (unsigned char)key_str[0];
    }

    // If no match, default to Ctrl+O
    return 15;
}

int load_config(config_t *config) {
    if (!config) return -1;

    // Set defaults
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

    if (!fp) {
        return -1; // Config file not found, using defaults
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    fread(buffer, 1, file_size, fp);
    buffer[file_size] = '\0';
    fclose(fp);

    json_object *root = json_tokener_parse(buffer);
    free(buffer);

    if (!root) {
        return -1;
    }

    // First, determine which provider we're using
    char selected_provider[64] = "openai"; // default
    json_object *llm_obj;
    if (json_object_object_get_ex(root, "llm", &llm_obj)) {
        json_object *provider_obj;
        if (json_object_object_get_ex(llm_obj, "provider", &provider_obj)) {
            snprintf(selected_provider, sizeof(selected_provider), "%s",
                     json_object_get_string(provider_obj));
        }
    }
    snprintf(config->llm.provider, sizeof(config->llm.provider), "%s", selected_provider);

    // Load default configuration from providers section
    json_object *providers_obj;
    if (json_object_object_get_ex(root, "providers", &providers_obj)) {
        json_object *provider_config;
        if (json_object_object_get_ex(providers_obj, selected_provider, &provider_config)) {
            // Load default model
            json_object *model_obj;
            if (json_object_object_get_ex(provider_config, "model", &model_obj)) {
                snprintf(config->llm.model, sizeof(config->llm.model), "%s",
                         json_object_get_string(model_obj));
            }
            // Load default endpoint
            json_object *endpoint_obj;
            if (json_object_object_get_ex(provider_config, "endpoint", &endpoint_obj)) {
                snprintf(config->llm.endpoint, sizeof(config->llm.endpoint), "%s",
                         json_object_get_string(endpoint_obj));
            }
        }
    }

    // Override with user's custom llm configuration if provided
    if (json_object_object_get_ex(root, "llm", &llm_obj)) {
        json_object *model_obj;
        if (json_object_object_get_ex(llm_obj, "model", &model_obj)) {
            snprintf(config->llm.model, sizeof(config->llm.model), "%s",
                     json_object_get_string(model_obj));
        }

        json_object *endpoint_obj;
        if (json_object_object_get_ex(llm_obj, "endpoint", &endpoint_obj)) {
            snprintf(config->llm.endpoint, sizeof(config->llm.endpoint), "%s",
                     json_object_get_string(endpoint_obj));
        }

        json_object *api_key_obj;
        if (json_object_object_get_ex(llm_obj, "api_key", &api_key_obj)) {
            snprintf(config->llm.api_key, sizeof(config->llm.api_key), "%s",
                     json_object_get_string(api_key_obj));
        }
    }

    // Environment variables have highest priority for API keys
    const char *env_api_key = NULL;
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

    // Parse trigger key
    json_object *trigger_obj;
    if (json_object_object_get_ex(root, "trigger_key", &trigger_obj)) {
        const char *trigger_str = json_object_get_string(trigger_obj);
        snprintf(config->trigger_key, sizeof(config->trigger_key), "%s", trigger_str);
        config->trigger_key_value = parse_keybinding(trigger_str);
    }

    // Parse proxy mode setting
    json_object *proxy_obj;
    if (json_object_object_get_ex(root, "enable_proxy_mode", &proxy_obj)) {
        config->enable_proxy_mode = json_object_get_boolean(proxy_obj);
    }

    // Parse startup messages setting
    json_object *startup_obj;
    if (json_object_object_get_ex(root, "show_startup_messages", &startup_obj)) {
        config->show_startup_messages = json_object_get_boolean(startup_obj);
    }

    json_object_put(root);
    return 0;
}

char* get_default_bin_path(const char* binary_name) {
    if (!binary_name) return NULL;

    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char* path = malloc(strlen(home) + strlen("/.local/bin/") + strlen(binary_name) + 1);
    if (!path) return NULL;

    sprintf(path, "%s/.local/bin/%s", home, binary_name);
    return path;
}

char* get_config_file_path(void) {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char* path = malloc(strlen(home) + strlen("/.config/smart-cmd/config.json") + 1);
    if (!path) return NULL;

    sprintf(path, "%s/.config/smart-cmd/config.json", home);
    return path;
}

char* get_temp_file_path(const char* prefix) {
    const char* tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";

    char session_id[32];
    if (generate_session_id(session_id, sizeof(session_id)) == -1) {
        return NULL;
    }

    char* path = malloc(strlen(tmp_dir) + strlen("/smart-cmd.") + strlen(prefix) + strlen(session_id) + 3);
    if (!path) return NULL;

    sprintf(path, "%s/smart-cmd.%s.%s", tmp_dir, prefix, session_id);
    return path;
}

