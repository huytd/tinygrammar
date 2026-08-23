#include "grammar.h"
#include "tokenizer.h"
#include "inference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/stat.h>

#define PROMPT_PREFIX "gec: "
#define PROMPT_SUFFIX "</s>"
#define MAX_TOKENS 512
#define MAX_OUTPUT_BYTES 4096

static Tokenizer* g_tokenizer = NULL;
static InferenceCtx g_ctx;
static int g_initialized = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static int file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int find_model_file(const char* filename, char* out_path, size_t max_len) {
    // 1. Check current directory: model/<filename>
    snprintf(out_path, max_len, "model/%s", filename);
    if (file_exists(out_path)) return 1;

    // 2. Check current directory: <filename>
    snprintf(out_path, max_len, "%s", filename);
    if (file_exists(out_path)) return 1;

    // 3. Check relative to executable
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char* dir = dirname(exe_path);

        // exe_dir/model/<filename>
        snprintf(out_path, max_len, "%s/model/%s", dir, filename);
        if (file_exists(out_path)) return 1;

        // exe_dir/../model/<filename>
        snprintf(out_path, max_len, "%s/../model/%s", dir, filename);
        if (file_exists(out_path)) return 1;

        // exe_dir/<filename>
        snprintf(out_path, max_len, "%s/%s", dir, filename);
        if (file_exists(out_path)) return 1;
    }

    return 0;
}

int grammar_init(void) {
    pthread_mutex_lock(&g_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        return 0;
    }

    char tok_path[1024];
    char enc_path[1024];
    char dec_path[1024];

    if (!find_model_file("tokenizer.json", tok_path, sizeof(tok_path))) {
        fprintf(stderr, "Grammar error: cannot locate tokenizer.json\n");
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }
    if (!find_model_file("encoder_model.onnx", enc_path, sizeof(enc_path))) {
        fprintf(stderr, "Grammar error: cannot locate encoder_model.onnx\n");
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }
    if (!find_model_file("decoder_model.onnx", dec_path, sizeof(dec_path))) {
        fprintf(stderr, "Grammar error: cannot locate decoder_model.onnx\n");
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    g_tokenizer = (Tokenizer*)calloc(1, sizeof(Tokenizer));
    if (!g_tokenizer) {
        fprintf(stderr, "Grammar error: failed to allocate tokenizer memory\n");
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    if (tokenizer_load(g_tokenizer, tok_path) != 0) {
        fprintf(stderr, "Grammar error: failed to load tokenizer: %s\n", tok_path);
        free(g_tokenizer);
        g_tokenizer = NULL;
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    if (inference_load(&g_ctx, enc_path, dec_path) != 0) {
        fprintf(stderr, "Grammar error: failed to load ONNX models\n");
        free(g_tokenizer);
        g_tokenizer = NULL;
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    g_initialized = 1;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int grammar_is_ready(void) {
    return g_initialized;
}

char* grammar_fix(const char* input_text) {
    if (!input_text) return NULL;

    // Skip leading/trailing whitespace checks if text is empty
    const char* p = input_text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '\0') {
        return strdup("");
    }

    pthread_mutex_lock(&g_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        if (grammar_init() != 0) {
            return NULL;
        }
        pthread_mutex_lock(&g_mutex);
    }

    // Prepare prompt: "gec: <input></s>"
    size_t in_len = strlen(input_text);
    size_t prompt_len = strlen(PROMPT_PREFIX) + in_len + strlen(PROMPT_SUFFIX) + 1;
    char* prompt = (char*)malloc(prompt_len);
    if (!prompt) {
        pthread_mutex_unlock(&g_mutex);
        return NULL;
    }

    snprintf(prompt, prompt_len, "%s%s%s", PROMPT_PREFIX, input_text, PROMPT_SUFFIX);

    int input_ids[MAX_TOKENS];
    int input_len = tokenizer_encode(g_tokenizer, prompt, input_ids, MAX_TOKENS);
    free(prompt);

    if (input_len <= 0) {
        pthread_mutex_unlock(&g_mutex);
        return strdup(input_text);
    }

    int output_ids[MAX_TOKENS];
    int output_len = inference_run(&g_ctx, input_ids, input_len, output_ids, MAX_TOKENS);

    char decoded[MAX_OUTPUT_BYTES];
    decoded[0] = '\0';
    tokenizer_decode(g_tokenizer, output_ids, output_len, decoded, sizeof(decoded));

    pthread_mutex_unlock(&g_mutex);

    // If decoded result is empty, return duplicate of original
    if (decoded[0] == '\0') {
        return strdup(input_text);
    }

    return strdup(decoded);
}

void grammar_cleanup(void) {
    pthread_mutex_lock(&g_mutex);
    if (g_initialized) {
        inference_free(&g_ctx);
        if (g_tokenizer) {
            free(g_tokenizer);
            g_tokenizer = NULL;
        }
        g_initialized = 0;
    }
    pthread_mutex_unlock(&g_mutex);
}
