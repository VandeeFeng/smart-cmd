#define _GNU_SOURCE
#include "smart_cmd.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdarg.h>

typedef struct {
    char *data;
    size_t size;
} response_buffer_t;

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
    size_t pos;
} dynamic_buffer_t;

static dynamic_buffer_t dynamic_buffer_create(size_t initial_capacity) {
    char *buffer = malloc(initial_capacity);
    return (dynamic_buffer_t){buffer, initial_capacity, initial_capacity, 0};
}

static void dynamic_buffer_destroy(dynamic_buffer_t *buf) {
    if (buf->buffer) {
        free(buf->buffer);
        buf->buffer = NULL;
        buf->size = buf->capacity = buf->pos = 0;
    }
}

static int dynamic_buffer_append(dynamic_buffer_t *buf, const char *format, ...) {
    if (!buf || !buf->buffer) return -1;

    va_list args;
    va_start(args, format);

    size_t available = buf->capacity - buf->pos - 1;
    int written = vsnprintf(buf->buffer + buf->pos, available, format, args);

    if (written > 0 && (size_t)written >= available) {
        size_t new_capacity = buf->capacity * 2;
        char *new_buffer = realloc(buf->buffer, new_capacity);
        if (!new_buffer) {
            va_end(args);
            return -1;
        }

        buf->buffer = new_buffer;
        buf->capacity = new_capacity;
        available = buf->capacity - buf->pos - 1;

        va_end(args);
        va_start(args, format);
        written = vsnprintf(buf->buffer + buf->pos, available, format, args);
    }

    if (written > 0) {
        buf->pos += written;
        buf->buffer[buf->pos] = '\0';
    }

    va_end(args);
    return written;
}

typedef struct {
    double temperature;
    int max_tokens;
    const char *model;
    const char *provider;
    const char *api_key;
    const char *endpoint;
} llm_request_params_t;

static llm_request_params_t create_llm_params_from_config(const config_t *config) {
    return (llm_request_params_t){
        .temperature = 0.7,
        .max_tokens = 100,
        .model = config->llm.model,
        .provider = config->llm.provider,
        .api_key = config->llm.api_key,
        .endpoint = config->llm.endpoint
    };
}

typedef struct {
    const char *last_command;
    const char *terminal_buffer;
} prompt_context_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, response_buffer_t *response) {
    size_t total_size = size * nmemb;
    char *ptr = realloc(response->data, response->size + total_size + 1);
    if (!ptr) return 0;

    response->data = ptr;
    memcpy(response->data + response->size, contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';

    return total_size;
}

static void build_system_prompt(char *buffer, size_t buffer_size, const prompt_context_t *ctx) {
    dynamic_buffer_t buf = dynamic_buffer_create(1024);

    dynamic_buffer_append(&buf, "%s",
        "You are an AI command-line assistant. Your goal is to complete the user's command or suggest the next one.\n\n"
        "CONTEXT:\n");

    if (ctx->terminal_buffer && strlen(ctx->terminal_buffer) > 0) {
        dynamic_buffer_append(&buf, "Command History:\n%s\n", ctx->terminal_buffer);
    }

    dynamic_buffer_append(&buf, "%s",
        "\nRULES:\n"
        "1. Your response must be a single command-line suggestion.\n"
        "2. If you are completing the user's partial command, your response MUST start with '+' followed by the ENTIRE completed command. Example: If the user input is 'git commi', your response should be '+git commit'.\n"
        "3. If you are suggesting a new command (not a completion of partial input), your response MUST start with '='. Example: '=git status'.\n"
        "4. Do NOT add any explanation. Your entire output must be just the prefix ('+' or '=') and the command.\n");

    strncpy(buffer, buf.buffer, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    dynamic_buffer_destroy(&buf);
}

typedef struct {
    const char *key;
    union {
        const char *str_val;
        double dbl_val;
        int int_val;
    };
    enum { JSON_STR, JSON_DBL, JSON_INT } type;
} json_field_t;

typedef struct {
    json_object *obj;
    int field_count;
} json_builder_t;

static json_builder_t json_builder_create() {
    return (json_builder_t){json_object_new_object(), 0};
}

static void json_builder_add_field(json_builder_t *builder, const char *key, const char *value) {
    json_object_object_add(builder->obj, key, json_object_new_string(value));
    builder->field_count++;
}

static void json_builder_add_double(json_builder_t *builder, const char *key, double value) {
    json_object_object_add(builder->obj, key, json_object_new_double(value));
    builder->field_count++;
}


static void json_object_add_field(json_object *obj, const char *key, const char *value) {
    json_object_object_add(obj, key, json_object_new_string(value));
}

static void json_object_add_double(json_object *obj, const char *key, double value) {
    json_object_object_add(obj, key, json_object_new_double(value));
}

static void json_object_add_int(json_object *obj, const char *key, int value) {
    json_object_object_add(obj, key, json_object_new_int(value));
}


static json_object *create_json_message(const char *role, const char *content) {
    json_builder_t builder = json_builder_create();
    json_builder_add_field(&builder, "role", role);
    json_builder_add_field(&builder, "content", content);
    return builder.obj;
}

static json_object *create_gemini_request(const char *input, const char *system_prompt, const llm_request_params_t *params) {
    json_object *request = json_object_new_object();
    json_object *contents = json_object_new_array();
    json_object *content = json_object_new_object();
    json_object *parts = json_object_new_array();

    dynamic_buffer_t prompt_buf = dynamic_buffer_create(1024);
    dynamic_buffer_append(&prompt_buf, "%s\n\nUser input: %s", system_prompt, input);

    json_builder_t part_builder = json_builder_create();
    json_builder_add_field(&part_builder, "text", prompt_buf.buffer);
    dynamic_buffer_destroy(&prompt_buf);
    json_object_array_add(parts, part_builder.obj);

    json_object_object_add(content, "parts", parts);
    json_object_array_add(contents, content);
    json_object_object_add(request, "contents", contents);

    json_builder_t config_builder = json_builder_create();
    json_builder_add_double(&config_builder, "temperature", params->temperature);
    json_object_object_add(request, "generationConfig", config_builder.obj);

    return request;
}

static json_object *create_openai_request(const char *input, const char *system_prompt, const llm_request_params_t *params) {
    json_object *request = json_object_new_object();
    json_object *messages = json_object_new_array();

    json_object_array_add(messages, create_json_message("system", system_prompt));
    json_object_array_add(messages, create_json_message("user", input));

    json_object_object_add(request, "messages", messages);

    json_object_add_field(request, "model", params->model);
    json_object_add_double(request, "temperature", params->temperature);
    json_object_add_int(request, "max_tokens", params->max_tokens);

    return request;
}

static json_object* create_llm_request(const char *input, const session_context_t *ctx, const config_t *config) {
    prompt_context_t prompt_ctx = {
        .last_command = ctx->last_command,
        .terminal_buffer = ctx->terminal_buffer
    };

    llm_request_params_t params = create_llm_params_from_config(config);

    char system_prompt[4096];
    build_system_prompt(system_prompt, sizeof(system_prompt), &prompt_ctx);

    if (strcmp(params.provider, "gemini") == 0) {
        return create_gemini_request(input, system_prompt, &params);
    } else {
        return create_openai_request(input, system_prompt, &params);
    }
}


static int parse_llm_response(const char *response_json, suggestion_t *suggestion, const char *provider) {
    json_object *root = json_tokener_parse(response_json);
    if (!root) return -1;

    const char *response_text = NULL;

    if (strcmp(provider, "gemini") == 0) {
        // Parse Gemini response
        json_object *candidates;
        if (json_object_object_get_ex(root, "candidates", &candidates)) {
            if (json_object_array_length(candidates) > 0) {
                json_object *candidate = json_object_array_get_idx(candidates, 0);
                json_object *content;
                if (json_object_object_get_ex(candidate, "content", &content)) {
                    json_object *parts;
                    if (json_object_object_get_ex(content, "parts", &parts)) {
                        if (json_object_array_length(parts) > 0) {
                            json_object *part = json_object_array_get_idx(parts, 0);
                            json_object *text;
                            if (json_object_object_get_ex(part, "text", &text)) {
                                response_text = json_object_get_string(text);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Parse OpenAI/OpenRouter response
        json_object *choices;
        if (json_object_object_get_ex(root, "choices", &choices)) {
            if (json_object_array_length(choices) > 0) {
                json_object *choice = json_object_array_get_idx(choices, 0);
                json_object *message;
                if (json_object_object_get_ex(choice, "message", &message)) {
                    json_object *content;
                    if (json_object_object_get_ex(message, "content", &content)) {
                        response_text = json_object_get_string(content);
                    }
                }
            }
        }
    }

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


static int send_http_request(const char *url, const char *api_key, json_object *request_json,
                             response_buffer_t *response, const char *provider) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (api_key && strlen(api_key) > 0) {
        char auth_header[512];
        if (strcmp(provider, "gemini") == 0) {
            // Gemini uses x-goog-api-key header
            snprintf(auth_header, sizeof(auth_header), "x-goog-api-key: %s", api_key);
        } else {
            // OpenAI and others use Authorization: Bearer
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        }
        headers = curl_slist_append(headers, auth_header);
    }

    const char *request_string = json_object_to_json_string(request_json);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_string);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

int send_to_llm(const char *input, const session_context_t *ctx, const config_t *config, suggestion_t *suggestion) {
    if (!input || !ctx || !config || !suggestion) return -1;

    // Validate provider
    if (strcmp(config->llm.provider, "openai") != 0 &&
        strcmp(config->llm.provider, "openrouter") != 0 &&
        strcmp(config->llm.provider, "gemini") != 0) {
        fprintf(stderr, "Unsupported LLM provider: %s\n", config->llm.provider);
        return -1;
    }

    // Initialize suggestion
    memset(suggestion, 0, sizeof(suggestion_t));

    // Create unified request
    json_object *request_json = create_llm_request(input, ctx, config);
    if (!request_json) {
        fprintf(stderr, "Failed to create JSON request\n");
        return -1;
    }

    // Build full endpoint URL for Gemini
    char full_endpoint[512];
    if (strcmp(config->llm.provider, "gemini") == 0) {
        snprintf(full_endpoint, sizeof(full_endpoint), "%s%s:generateContent",
                 config->llm.endpoint, config->llm.model);
    } else {
        strncpy(full_endpoint, config->llm.endpoint, sizeof(full_endpoint) - 1);
    }

    // Send HTTP request
    response_buffer_t response = {0};
    int result = send_http_request(full_endpoint, config->llm.api_key, request_json, &response, config->llm.provider);

    if (result == 0 && response.data) {
        result = parse_llm_response(response.data, suggestion, config->llm.provider);
    } else {
        fprintf(stderr, "HTTP request failed or no response data\n");
        result = -1;
    }

    // Cleanup
    if (response.data) free(response.data);
    json_object_put(request_json);

    return result;
}
