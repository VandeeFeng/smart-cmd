#define _GNU_SOURCE
#include "smart_cmd.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdarg.h>

// Consolidated LLM request context
typedef struct {
    const char *last_command;
    const char *terminal_buffer;
    char *response_data;
    size_t response_size;
} llm_context_t;

// HTTP request structure
typedef struct {
    char method[8];
    char endpoint[512];
    char headers[10][300];
    int header_count;
    char *body;
} http_request_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, llm_context_t *ctx) {
    size_t total_size = size * nmemb;
    char *ptr = realloc(ctx->response_data, ctx->response_size + total_size + 1);
    if (!ptr) return 0;

    ctx->response_data = ptr;
    memcpy(ctx->response_data + ctx->response_size, contents, total_size);
    ctx->response_size += total_size;
    ctx->response_data[ctx->response_size] = '\0';

    return total_size;
}

static void build_system_prompt(char *buffer, size_t buffer_size, const llm_context_t *ctx) {
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

static char* build_gemini_json(const char *system_prompt, const char *input) {
    json_object *root = json_object_new_object();
    json_object *contents = json_object_new_array();
    json_object *content = json_object_new_object();
    json_object *parts = json_object_new_array();
    json_object *part = json_object_new_object();
    char prompt[4110];
    snprintf(prompt, sizeof(prompt), "%s\n\nUser input: %s", system_prompt, input);
    json_object *text = json_object_new_string(prompt);

    json_object_object_add(part, "text", text);
    json_object_array_add(parts, part);
    json_object_object_add(content, "parts", parts);
    json_object_array_add(contents, content);

    json_object *generation_config = json_object_new_object();
    json_object_object_add(generation_config, "temperature", json_object_new_double(0.7));

    json_object_object_add(root, "contents", contents);
    json_object_object_add(root, "generationConfig", generation_config);

    const char *json_string = json_object_to_json_string(root);
    char *result = strdup(json_string);
    json_object_put(root);

    return result;
}

static char* build_openai_json(const char *system_prompt, const char *input, const char *model) {
    json_object *root = json_object_new_object();
    json_object *messages = json_object_new_array();

    json_object *system_msg = json_object_new_object();
    json_object_object_add(system_msg, "role", json_object_new_string("system"));
    json_object_object_add(system_msg, "content", json_object_new_string(system_prompt));
    json_object_array_add(messages, system_msg);

    json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(input));
    json_object_array_add(messages, user_msg);

    json_object_object_add(root, "messages", messages);
    json_object_object_add(root, "model", json_object_new_string(model));
    json_object_object_add(root, "temperature", json_object_new_double(0.7));
    json_object_object_add(root, "max_tokens", json_object_new_int(100));

    const char *json_string = json_object_to_json_string(root);
    char *result = strdup(json_string);
    json_object_put(root);

    return result;
}

static void build_llm_request(http_request_t *request, const char *input, const char *system_prompt, const config_t *config) {
    strncpy(request->method, "POST", sizeof(request->method) - 1);
    request->method[sizeof(request->method) - 1] = '\0';

    if (strcmp(config->llm.provider, "gemini") == 0) {
        snprintf(request->endpoint, sizeof(request->endpoint), "%s%s:generateContent",
                 config->llm.endpoint, config->llm.model);
        request->body = build_gemini_json(system_prompt, input);
    } else {
        strncpy(request->endpoint, config->llm.endpoint, sizeof(request->endpoint) - 1);
        request->endpoint[sizeof(request->endpoint) - 1] = '\0';
        request->body = build_openai_json(system_prompt, input, config->llm.model);
    }

    strncpy(request->headers[0], "Content-Type: application/json", sizeof(request->headers[0]) - 1);
    request->headers[0][sizeof(request->headers[0]) - 1] = '\0';
    request->header_count = 1;

    if (strlen(config->llm.api_key) > 0) {
        if (strcmp(config->llm.provider, "gemini") == 0) {
            snprintf(request->headers[1], sizeof(request->headers[1]), "x-goog-api-key: %s", config->llm.api_key);
        } else {
            snprintf(request->headers[1], sizeof(request->headers[1]), "Authorization: Bearer %s", config->llm.api_key);
        }
        request->header_count++;
    }
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


static int send_http_request(const http_request_t *request, llm_context_t *ctx) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    // Build headers from request structure
    struct curl_slist *headers = NULL;
    for (int i = 0; i < request->header_count; i++) {
        headers = curl_slist_append(headers, request->headers[i]);
    }

    // Send request
    curl_easy_setopt(curl, CURLOPT_URL, request->endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

int send_to_llm(const char *input, const session_context_t *ctx, const config_t *config, suggestion_t *suggestion) {
    if (!input || !ctx || !config || !suggestion) return -1;

    if (strcmp(config->llm.provider, "openai") != 0 &&
        strcmp(config->llm.provider, "openrouter") != 0 &&
        strcmp(config->llm.provider, "gemini") != 0) {
        fprintf(stderr, "Unsupported LLM provider: %s\n", config->llm.provider);
        return -1;
    }

    memset(suggestion, 0, sizeof(suggestion_t));

    llm_context_t llm_ctx = {
        .last_command = ctx->last_command,
        .terminal_buffer = ctx->terminal_buffer,
        .response_data = NULL,
        .response_size = 0
    };

    char system_prompt[4096];
    build_system_prompt(system_prompt, sizeof(system_prompt), &llm_ctx);

    http_request_t request = {0};
    build_llm_request(&request, input, system_prompt, config);

    int result = send_http_request(&request, &llm_ctx);

    if (result == 0 && llm_ctx.response_data) {
        result = parse_llm_response(llm_ctx.response_data, suggestion, config->llm.provider);
    } else {
        fprintf(stderr, "HTTP request failed or no response data\n");
        result = -1;
    }

    if (request.body) free(request.body);
    if (llm_ctx.response_data) free(llm_ctx.response_data);

    return result;
}
