#define _GNU_SOURCE
#include "smart_cmd.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdarg.h>
#include <stdbool.h>


// Request parameters structure for builder functions
typedef struct {
    const char *system_prompt;
    const char *model;
    double temperature;
    int max_tokens;
    const char *input;
    const char *endpoint;
} request_params_t;

// Unified request context for LLM operations
typedef struct {
    // HTTP request components
    char headers[MAX_HEADERS][MAX_HEADER_LENGTH];
    int header_count;
    char *request_body;

    // LLM context and response
    const char *last_command;
    const char *terminal_buffer;
    const char *input;
    char *response_data;
    size_t response_size;
} request_context_t;

// Provider request structure
typedef struct {
    char *request_body;
    char endpoint[MAX_ENDPOINT_LENGTH];
} provider_request_t;

// Provider builder function type
typedef provider_request_t* (*provider_builder_func_t)(const request_params_t *params);

// Extended provider configuration structure
typedef struct {
    const char *name;
    provider_builder_func_t builder;
    const char *default_endpoint;
    const char *default_model;
    bool needs_auth_token;
    bool model_in_url;
} provider_config_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, request_context_t *ctx) {
    size_t total_size = size * nmemb;
    char *ptr = realloc(ctx->response_data, ctx->response_size + total_size + 1);
    if (!ptr) return 0;

    ctx->response_data = ptr;
    memcpy(ctx->response_data + ctx->response_size, contents, total_size);
    ctx->response_size += total_size;
    ctx->response_data[ctx->response_size] = '\0';

    return total_size;
}

static void build_system_prompt(char *buffer, size_t buffer_size, const request_context_t *ctx) {
    int pos = 0;

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "You are an AI command-line assistant. Your goal is to complete the user's command or suggest the next one.\n\n"
                    "CONTEXT:\n");

    if (ctx->terminal_buffer && strlen(ctx->terminal_buffer) > 0) {
        pos += snprintf(buffer + pos, buffer_size - pos, "Command History:\n%s\n", ctx->terminal_buffer);
    }

    snprintf(buffer + pos, buffer_size - pos,
             "\nRULES:\n"
             "1. Your response must be a single command-line suggestion.\n"
             "2. If you are completing the user's partial command, your response MUST start with '+' followed by the ENTIRE completed command. Example: If the user input is 'git commi', your response should be '+git commit'.\n"
             "3. If you are suggesting a new command (not a completion of partial input), your response MUST start with '='. Example: '=git status'.\n"
             "4. Do NOT add any explanation. Your entire output must be just the prefix ('+' or '=') and the command.\n");
}

// Unified message creation function
static json_object* create_message(const char *role, const char *content) {
    json_object *message = json_object_new_object();
    json_object_object_add(message, "role", json_object_new_string(role));
    json_object_object_add(message, "content", json_object_new_string(content));
    return message;
}

// Build OpenAI-style messages array
static json_object* build_openai_messages(const char *system_prompt, const char *input) {
    json_object *messages = json_object_new_array();

    json_object *system_msg = create_message("system", system_prompt);
    json_object_array_add(messages, system_msg);

    json_object *user_msg = create_message("user", input);
    json_object_array_add(messages, user_msg);

    return messages;
}

// Build Gemini-style content structure
static json_object* build_gemini_content(const char *system_prompt, const char *input) {
    json_object *contents = json_object_new_array();
    json_object *content = json_object_new_object();
    json_object *parts = json_object_new_array();
    json_object *part = json_object_new_object();

    char prompt[MAX_PROMPT_LENGTH];
    snprintf(prompt, sizeof(prompt), "%s\n\nUser input: %s", system_prompt, input);

    json_object_object_add(part, "text", json_object_new_string(prompt));
    json_object_array_add(parts, part);
    json_object_object_add(content, "parts", parts);
    json_object_array_add(contents, content);

    return contents;
}

// OpenAI provider request builder
static provider_request_t* build_openai_request(const request_params_t *params) {
    json_object *root = json_object_new_object();
    json_object *messages = build_openai_messages(params->system_prompt, params->input);

    json_object_object_add(root, "messages", messages);
    json_object_object_add(root, "model", json_object_new_string(params->model));
    json_object_object_add(root, "temperature", json_object_new_double(params->temperature));
    json_object_object_add(root, "max_tokens", json_object_new_int(params->max_tokens));

    provider_request_t *request = malloc(sizeof(provider_request_t));
    if (!request) {
        json_object_put(root);
        return NULL;
    }

    const char *json_string = json_object_to_json_string(root);
    request->request_body = strdup(json_string);
    strncpy(request->endpoint, params->endpoint, sizeof(request->endpoint) - 1);
    request->endpoint[sizeof(request->endpoint) - 1] = '\0';

    json_object_put(root);
    return request;
}

// Gemini provider request builder
static provider_request_t* build_gemini_request(const request_params_t *params) {
    json_object *root = json_object_new_object();
    json_object *contents = build_gemini_content(params->system_prompt, params->input);
    json_object *generation_config = json_object_new_object();

    // Add generation configuration
    json_object_object_add(generation_config, "temperature", json_object_new_double(params->temperature));
    json_object_object_add(generation_config, "maxOutputTokens", json_object_new_int(params->max_tokens));

    json_object_object_add(root, "contents", contents);
    json_object_object_add(root, "generationConfig", generation_config);

    provider_request_t *request = malloc(sizeof(provider_request_t));
    if (!request) {
        json_object_put(root);
        return NULL;
    }

    const char *json_string = json_object_to_json_string(root);
    request->request_body = strdup(json_string);

    // Build full endpoint URL for Gemini: endpoint + model + :generateContent
    snprintf(request->endpoint, sizeof(request->endpoint), "%s%s:generateContent", params->endpoint, params->model);

    json_object_put(root);
    return request;
}

// Unified provider registry
static const provider_config_t providers[] = {
    {
        "openai",
        build_openai_request,
        "https://api.openai.com/v1/chat/completions",
        "gpt-4.1-nano",
        true,
        false
    },
    {
        "openrouter",
        build_openai_request,
        "https://openrouter.ai/api/v1/chat/completions",
        "qwen/qwen3-coder:free",
        true,
        false
    },
    {
        "gemini",
        build_gemini_request,
        "https://generativelanguage.googleapis.com/v1beta/models/",
        "gemini-2.0-flash",
        false,
        true
    },
    {NULL, NULL, NULL, NULL, false, false}
};

// Provider configuration lookup function
static const provider_config_t* get_provider_config(const char *provider_name) {
    for (int i = 0; providers[i].name; i++) {
        if (strcmp(provider_name, providers[i].name) == 0) {
            return &providers[i];
        }
    }
    return NULL;
}

// Dynamic provider support validation
static int is_provider_supported(const char *provider_name) {
    for (int i = 0; providers[i].name; i++) {
        if (strcmp(provider_name, providers[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Setup authentication headers based on provider configuration
static void setup_auth_headers(request_context_t *request, const config_t *config,
                               const provider_config_t *provider) {
    if (strlen(config->llm.api_key) == 0) return;

    if (provider->needs_auth_token) {
        snprintf(request->headers[1], sizeof(request->headers[1]),
                 "Authorization: Bearer %s", config->llm.api_key);
    } else {
        snprintf(request->headers[1], sizeof(request->headers[1]),
                 "x-goog-api-key: %s", config->llm.api_key);
    }
    request->header_count++;
}

static provider_request_t* build_llm_request(const char *system_prompt, const config_t *config, const char *input) {
    const provider_config_t *provider = get_provider_config(config->llm.provider);
    if (!provider) return NULL;

    // Use user config if provided, otherwise use defaults
    request_params_t params = {
        .system_prompt = system_prompt,
        .model = config->llm.model[0] ? config->llm.model : provider->default_model,
        .temperature = 0.7,
        .max_tokens = 100,
        .input = input,
        .endpoint = config->llm.endpoint[0] ? config->llm.endpoint : provider->default_endpoint
    };

    return provider->builder(&params);
}

static json_object* get_json_value_by_path(json_object *obj, const char *path) {
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = strtok(path_copy, ".");
    json_object *current = obj;

    while (token && current) {
        if (json_object_get_type(current) == json_type_array) {
            int index = atoi(token);
            if (index < 0 || index >= (int)json_object_array_length(current)) {
                return NULL;
            }
            current = json_object_array_get_idx(current, index);
        } else {
            if (!json_object_object_get_ex(current, token, &current)) {
                return NULL;
            }
        }
        token = strtok(NULL, ".");
    }

    return current;
}

static int parse_llm_response(const char *response_json, suggestion_t *suggestion, const char *provider) {
    json_object *root = json_tokener_parse(response_json);
    if (!root) return -1;

    json_object *response_text_obj = NULL;

    if (strcmp(provider, "gemini") == 0) {
        response_text_obj = get_json_value_by_path(root, "candidates.0.content.parts.0.text");
    } else {
        response_text_obj = get_json_value_by_path(root, "choices.0.message.content");
    }

    const char *response_text = response_text_obj ? json_object_get_string(response_text_obj) : NULL;

    if (response_text && strlen(response_text) > 0) {
        suggestion->type = response_text[0];
        strncpy(suggestion->suggestion, response_text + 1,
                sizeof(suggestion->suggestion) - 1);
        suggestion->suggestion[strcspn(suggestion->suggestion, "\n")] = 0;
        suggestion->visible = 1;
        json_object_put(root);
        return 0;
    }

    json_object_put(root);
    return -1;
}

static int send_http_request(const request_context_t *request, const char *endpoint) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    // Build headers from request structure
    struct curl_slist *headers = NULL;
    for (int i = 0; i < request->header_count; i++) {
        headers = curl_slist_append(headers, request->headers[i]);
    }

    // Send request
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->request_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)request);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

int send_to_llm(const char *input, const session_context_t *ctx, const config_t *config, suggestion_t *suggestion) {
    if (!input || !ctx || !config || !suggestion) return -1;

    if (!is_provider_supported(config->llm.provider)) {
        fprintf(stderr, "ERROR: send_to_llm: Unsupported LLM provider: %s\n", config->llm.provider);
        return -1;
    }

    memset(suggestion, 0, sizeof(suggestion_t));

    // Initialize unified request context
    request_context_t request = {0};
    request.last_command = ctx->last_command;
    request.terminal_buffer = ctx->terminal_buffer;
    request.input = input;
    request.response_data = NULL;
    request.response_size = 0;

    char system_prompt[MAX_SYSTEM_PROMPT_LENGTH];
    build_system_prompt(system_prompt, sizeof(system_prompt), &request);

    // Build provider request using new function
    provider_request_t *provider_request = build_llm_request(system_prompt, config, input);
    if (!provider_request) {
        fprintf(stderr, "ERROR: send_to_llm: Failed to build provider request\n");
        return -1;
    }

    // Setup request context
    request.request_body = provider_request->request_body;
    strncpy(request.headers[0], "Content-Type: application/json", sizeof(request.headers[0]) - 1);
    request.headers[0][sizeof(request.headers[0]) - 1] = '\0';
    request.header_count = 1;

    // Setup authentication using provider configuration
    const provider_config_t *provider = get_provider_config(config->llm.provider);
    if (provider) {
        setup_auth_headers(&request, config, provider);
    }

    int result = send_http_request(&request, provider_request->endpoint);

    if (result == 0 && request.response_data) {
        result = parse_llm_response(request.response_data, suggestion, config->llm.provider);
    } else {
        fprintf(stderr, "ERROR: send_to_llm: HTTP request failed or no response data\n");
        result = -1;
    }

    if (provider_request->request_body) free(provider_request->request_body);
    if (provider_request) free(provider_request);
    if (request.response_data) free(request.response_data);

    return result;
}
