#define _GNU_SOURCE
#include "smart_cmd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_BUFFER 8192
#define MAX_CONTENT 4096

typedef struct {
    char roles[5][12];
    char contents[2+MAX_HISTORY_MESSAGES][MAX_CONTENT];
    int msg_count;
} Agent;


static char* json_find(const char* json, const char* key, char* out, size_t size) {
    if (!json || !key || !out) return NULL;
    char pattern[64];
    snprintf(pattern, 64, "\"%s\":", key);
    const char* start = strstr(json, pattern);
    if (!start) return NULL;
    start += strlen(pattern);
    while (*start == ' ' || *start == '\t') start++;

    if (*start == '"') {
        start++;
        const char* end = start;
        while (*end && *end != '"') {
            if (*end == '\\' && end[1]) end += 2;
            else end++;
        }
        size_t len = end - start;
        if (len >= size) len = size - 1;
        strncpy(out, start, len);
        out[len] = '\0';

        for (char* p = out; *p; p++) {
            if (*p == '\\' && p[1]) {
                switch (p[1]) {
                case 'n': *p = '\n'; memmove(p+1, p+2, strlen(p+1)); break;
                case 't': *p = '\t'; memmove(p+1, p+2, strlen(p+1)); break;
                case 'r': *p = '\r'; memmove(p+1, p+2, strlen(p+1)); break;
                case '\\': case '"': memmove(p, p+1, strlen(p)); break;
                }
            }
        }
    } else {
        const char* end = start;
        while (*end && *end != ',' && *end != '}' && *end != ' ' && *end != '\n') end++;
        size_t len = end - start;
        if (len >= size) len = size - 1;
        strncpy(out, start, len);
        out[len] = '\0';
    }
    return out;
}

static void build_system_prompt(char* buffer, size_t buffer_size, const session_context_t* ctx) {
    int pos = 0;

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "You are an AI command-line assistant. Your goal is to complete the user's command or suggest the next one.\n\n"
                    "CONTEXT:\n");

    if (strlen(ctx->terminal_buffer) > 0) {
        pos += snprintf(buffer + pos, buffer_size - pos, "Command History:\n%s\n", ctx->terminal_buffer);
    }

    snprintf(buffer + pos, buffer_size - pos,
             "\nRULES:\n"
             "1. Your response must be a single command-line suggestion.\n"
             "2. If you are completing the user's partial command, your response MUST start with '+' followed by the ENTIRE completed command. Example: If the user input is 'git commi', your response should be '+git commit'.\n"
             "3. If you are suggesting a new command (not a completion of partial input), your response MUST start with '='. Example: '=git status'.\n"
             "4. Do NOT add any explanation. Your entire output must be just the prefix ('+' or '=') and the command.\n");
}

static char* json_request(const Agent* agent, const config_t* config, char* out, size_t size) {
    if (!agent || !out) return NULL;

    if (strcmp(config->llm.provider, "gemini") == 0) {
        // Gemini format: combine system+user into single message with parts
        char combined_prompt[MAX_CONTENT * 2] = "";
        for (int i = 0; i < agent->msg_count; i++) {
            if (strcmp(agent->roles[i], "system") == 0) {
                strcat(combined_prompt, agent->contents[i]);
                strcat(combined_prompt, "\n\n");
            } else if (strcmp(agent->roles[i], "user") == 0) {
                strcat(combined_prompt, agent->contents[i]);
            }
        }
        snprintf(out, size, "{\"contents\":[{\"parts\":[{\"text\":\"%s\"}]}],\"generationConfig\":{\"temperature\":0.7,\"maxOutputTokens\":100}}", combined_prompt);
    } else {
        // OpenAI format: standard messages array
        char messages[MAX_BUFFER] = "[";
        for (int i = 0; i < agent->msg_count; i++) {
            if (i > 0) strcat(messages, ",");
            char temp[MAX_CONTENT + 100];
            snprintf(temp, sizeof(temp), "{\"role\":\"%s\",\"content\":\"%s\"}", agent->roles[i], agent->contents[i]);
            if (strlen(messages) + strlen(temp) + 10 < sizeof(messages)) strcat(messages, temp);
        }
        strcat(messages, "]");
        const char* model = config->llm.model[0] ? config->llm.model : "gpt-4.1-nano";
        snprintf(out, size, "{\"model\":\"%s\",\"messages\":%s,\"temperature\":0.7,\"max_tokens\":100}", model, messages);
    }

    return out;
}

static char* json_content(const char* response, char* out, size_t size) {
    if (!response || !out) return NULL;

    // Try OpenAI format: choices[0].message.content
    const char* choices = strstr(response, "\"choices\":");
    if (choices) {
        const char* message = strstr(choices, "\"message\":");
        if (message) {
            return json_find(message, "content", out, size);
        }
    }

    // Try Gemini format: candidates[0].content.parts[0].text
    const char* candidates = strstr(response, "\"candidates\":");
    if (candidates) {
        const char* content = strstr(candidates, "\"content\":");
        if (content) {
            const char* parts = strstr(content, "\"parts\":");
            if (parts) {
                char temp[1024];
                if (json_find(parts, "text", temp, sizeof(temp))) {
                    strncpy(out, temp, size - 1);
                    out[size - 1] = '\0';
                    return out;
                }
            }
        }
    }

    return NULL;
}

static int http_request(const char* req, char* resp, size_t resp_size, const config_t* config) {
    char temp[] = "/tmp/ai_req_XXXXXX";
    int fd = mkstemp(temp);
    if (fd == -1) return -1;
    write(fd, req, strlen(req));
    close(fd);

    // Build endpoint URL
    char endpoint[512];
    if (strcmp(config->llm.provider, "gemini") == 0) {
        const char* model = config->llm.model[0] ? config->llm.model : "gemini-2.0-flash";
        const char* base_url = config->llm.endpoint[0] ? config->llm.endpoint : "https://generativelanguage.googleapis.com/v1beta/models/";
            snprintf(endpoint, sizeof(endpoint), "%s%s:generateContent", base_url, model);
    } else {
        const char* base_url = config->llm.endpoint[0] ? config->llm.endpoint : "https://api.openai.com/v1/chat/completions";
        strncpy(endpoint, base_url, sizeof(endpoint) - 1);
        endpoint[sizeof(endpoint) - 1] = '\0';
    }

    char curl_template[MAX_BUFFER];
    if (strcmp(config->llm.provider, "gemini") == 0) {
        snprintf(curl_template, sizeof(curl_template),
                 "curl -s -X POST '%s' -H 'Content-Type: application/json' -H 'x-goog-api-key: %s' -d @'%s' --max-time 60",
                 endpoint, config->llm.api_key, temp);
    } else {
        snprintf(curl_template, sizeof(curl_template),
                 "curl -s -X POST '%s' -H 'Content-Type: application/json' -H 'Authorization: Bearer %s' -d @'%s' --max-time 60",
                 endpoint, config->llm.api_key, temp);
    }

    FILE* pipe = popen(curl_template, "r");
    if (!pipe) { unlink(temp); return -1; }

    size_t bytes = fread(resp, 1, resp_size - 1, pipe);
    resp[bytes] = '\0';
    pclose(pipe);
    unlink(temp);
    return 0;
}

static int parse_llm_response(const char* response_json, suggestion_t* suggestion) {
    if (!response_json || !suggestion) return -1;

    char content[MAX_CONTENT];
    if (!json_content(response_json, content, sizeof(content))) {
        return -1;
    }

    if (strlen(content) > 0) {
        suggestion->type = content[0];
        strncpy(suggestion->suggestion, content + 1, sizeof(suggestion->suggestion) - 1);
        suggestion->suggestion[sizeof(suggestion->suggestion) - 1] = '\0';
        suggestion->suggestion[strcspn(suggestion->suggestion, "\n")] = 0;
        suggestion->visible = 1;
        return 0;
    }

    return -1;
}

int send_to_llm(const char *input, const session_context_t *ctx, const config_t *config, suggestion_t *suggestion) {
    if (!input || !ctx || !config || !suggestion) return -1;

    memset(suggestion, 0, sizeof(suggestion_t));

    Agent agent = {0};

    char system_prompt[MAX_CONTENT];
    build_system_prompt(system_prompt, sizeof(system_prompt), ctx);

    strcpy(agent.roles[0], "system");
    strncpy(agent.contents[0], system_prompt, MAX_CONTENT - 1);
    agent.contents[0][MAX_CONTENT - 1] = '\0';
    agent.msg_count = 1;

    strcpy(agent.roles[agent.msg_count], "user");
    strncpy(agent.contents[agent.msg_count], input, MAX_CONTENT - 1);
    agent.contents[agent.msg_count][MAX_CONTENT - 1] = '\0';
    agent.msg_count++;

    char req[MAX_BUFFER], resp[MAX_BUFFER];
    json_request(&agent, config, req, sizeof(req));

    if (http_request(req, resp, sizeof(resp), config)) {
        fprintf(stderr, "ERROR: send_to_llm: HTTP request failed\n");
        return -1;
    }

    int result = parse_llm_response(resp, suggestion);
    if (result != 0) {
        fprintf(stderr, "ERROR: send_to_llm: Failed to parse response\n");
    }

    return result;
}
