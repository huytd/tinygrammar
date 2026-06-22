#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"
#include "inference.h"

#define TOKENIZER_PATH "model/tokenizer.json"
#define ENCODER_PATH   "model/encoder_model.onnx"
#define DECODER_PATH   "model/decoder_model.onnx"
#define PROMPT_PREFIX  "gec: "

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <text>\n", argv[0]);
        return 1;
    }

    // calculate total length needed
    int total_len = strlen(PROMPT_PREFIX);
    for (int i = 1; i < argc; i++)
        total_len += strlen(argv[i]) + 1;  // +1 for space/null

    char* input = malloc(total_len + 1);
    if (!input) { fprintf(stderr, "OOM\n"); return 1; }

    // build "PROMPT_PREFIX arg1 arg2 ..."
    strcpy(input, PROMPT_PREFIX);
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(input, " ");
        strcat(input, argv[i]);
    }
    strcat(input, "</s>");

    Tokenizer tok;
    if (tokenizer_load(&tok, TOKENIZER_PATH) != 0) {
        fprintf(stderr, "Failed to load tokenizer.json\n");
        free(input); return 1;
    }

    InferenceCtx ctx;
    if (inference_load(&ctx, ENCODER_PATH, DECODER_PATH) != 0) {
        fprintf(stderr, "Failed to load models\n");
        free(input); return 1;
    }

    int input_ids[512];
    int input_len = tokenizer_encode(&tok, input, input_ids, 512);

    int output_ids[512];
    int output_len = inference_run(&ctx, input_ids, input_len, output_ids, 512);

    char result[2048];
    tokenizer_decode(&tok, output_ids, output_len, result, sizeof(result));

    printf("%s\n", result);

    free(input);
    inference_free(&ctx);
    return 0;
}
