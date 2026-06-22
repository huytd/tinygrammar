#pragma once
#include <onnxruntime_c_api.h>

typedef struct {
    OrtEnv*       env;
    OrtSession*   encoder;
    OrtSession*   decoder;
    OrtAllocator* allocator;
    const OrtApi* api;
} InferenceCtx;

int  inference_load(InferenceCtx* ctx, const char* encoder_path, const char* decoder_path);
int  inference_run(InferenceCtx* ctx, const int* input_ids, int input_len, int* out_ids, int max_out);
void inference_free(InferenceCtx* ctx);
