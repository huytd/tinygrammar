#include <stdio.h>
#include "tokenizer.h"
#include "inference.h"

int main() {
    Tokenizer tok;
    tokenizer_load(&tok, "model/tokenizer.json");

    InferenceCtx ctx;
    inference_load(&ctx, "model/encoder_model.onnx", "model/decoder_model.onnx");

    const char* input = "gec: Last weekend i sign up a gym membership. Since then I wnet swimming everyday.";

    int input_ids[256];
    int input_len = tokenizer_encode(&tok, input, input_ids, 256);

    int output_ids[256];
    int output_len = inference_run(&ctx, input_ids, input_len, output_ids, 256);

    char result[1024];
    tokenizer_decode(&tok, output_ids, output_len, result, sizeof(result));
    printf("Input:  %s\n", input);
    printf("Output: %s\n", result);

    inference_free(&ctx);
    return 0;
}
