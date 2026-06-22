#include "inference.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

#define EOS_TOKEN_ID  1
#define PAD_TOKEN_ID  0
#define MAX_SEQ       512

static const OrtApi* g_api = NULL;

static void check(OrtStatus* status) {
    if (status) {
        fprintf(stderr, "ORT error: %s\n", g_api->GetErrorMessage(status));
        g_api->ReleaseStatus(status);
        exit(1);
    }
}

int inference_load(InferenceCtx* ctx, const char* encoder_path, const char* decoder_path) {
    ctx->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    g_api    = ctx->api;
    const OrtApi* api = ctx->api;

    check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "tinygrammar", &ctx->env));

    OrtSessionOptions* opts;
    check(api->CreateSessionOptions(&opts));
    check(api->SetIntraOpNumThreads(opts, 1));

    check(api->CreateSession(ctx->env, encoder_path, opts, &ctx->encoder));
    check(api->CreateSession(ctx->env, decoder_path, opts, &ctx->decoder));

    check(api->GetAllocatorWithDefaultOptions(&ctx->allocator));
    api->ReleaseSessionOptions(opts);
    return 0;
}

static OrtValue* make_int64_tensor(const OrtApi* api,
                                   OrtAllocator* allocator,
                                   const int* data, int len) {
    int64_t shape[] = {1, len};
    OrtValue* tensor = NULL;
    check(api->CreateTensorAsOrtValue(allocator,
                                      shape, 2,
                                      ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                      &tensor));
    int64_t* buf;
    check(api->GetTensorMutableData(tensor, (void**)&buf));
    for (int i = 0; i < len; i++) buf[i] = (int64_t)data[i];
    return tensor;
}

int inference_run(InferenceCtx* ctx, const int* input_ids, int input_len, int* out_ids, int max_out) {
    const OrtApi* api = ctx->api;

    // build attention mask (all 1s)
    int* mask_data = malloc(input_len * sizeof(int));
    for (int i = 0; i < input_len; i++) mask_data[i] = 1;

    // --- encoder ---
    OrtValue* enc_input_ids = make_int64_tensor(api, ctx->allocator, input_ids,  input_len);
    OrtValue* enc_attn_mask = make_int64_tensor(api, ctx->allocator, mask_data,  input_len);

    const char* enc_in_names[]  = {"input_ids", "attention_mask"};
    const char* enc_out_names[] = {"last_hidden_state"};
    OrtValue*   enc_inputs[]    = {enc_input_ids, enc_attn_mask};
    OrtValue*   enc_outputs[]   = {NULL};

    check(api->Run(ctx->encoder, NULL,
                   enc_in_names, (const OrtValue* const*)enc_inputs,  2,
                   enc_out_names, 1, enc_outputs));

    OrtValue* hidden_state = enc_outputs[0];

    // --- decoder loop ---
    int dec_ids[MAX_SEQ] = {PAD_TOKEN_ID};  // T5 starts decoder with <pad>
    int dec_len  = 1;
    int out_len  = 0;

    while (out_len < max_out) {
        OrtValue* dec_input_ids = make_int64_tensor(api, ctx->allocator, dec_ids, dec_len);

        // order must match model: encoder_attention_mask, input_ids, encoder_hidden_states
        const char* dec_in_names[]  = {"encoder_attention_mask",
                                       "input_ids",
                                       "encoder_hidden_states"};
        const char* dec_out_names[] = {"logits"};
        OrtValue*   dec_inputs[]    = {enc_attn_mask, dec_input_ids, hidden_state};
        OrtValue*   dec_outputs[]   = {NULL};

        check(api->Run(ctx->decoder, NULL,
                       dec_in_names, (const OrtValue* const*)dec_inputs,  3,
                       dec_out_names, 1, dec_outputs));

        // logits shape: [1, dec_len, 32128]
        float* logits;
        check(api->GetTensorMutableData(dec_outputs[0], (void**)&logits));

        OrtTensorTypeAndShapeInfo* info;
        check(api->GetTensorTypeAndShape(dec_outputs[0], &info));
        int64_t dims[3];
        check(api->GetDimensions(info, dims, 3));
        int vocab_size = (int)dims[2];
        api->ReleaseTensorTypeAndShapeInfo(info);

        // argmax over last token position
        float* last_logits = logits + (dec_len - 1) * vocab_size;
        int   best_id  = 0;
        float best_val = -FLT_MAX;
        for (int v = 0; v < vocab_size; v++) {
            if (last_logits[v] > best_val) {
                best_val = last_logits[v];
                best_id  = v;
            }
        }

        api->ReleaseValue(dec_outputs[0]);
        api->ReleaseValue(dec_input_ids);

        if (best_id == EOS_TOKEN_ID) break;
        if (best_id == PAD_TOKEN_ID) break;

        if (out_len >= 4 &&
            out_ids[out_len - 1] == best_id &&
            out_ids[out_len - 2] == best_id &&
            out_ids[out_len - 3] == best_id) break;

        out_ids[out_len++] = best_id;
        dec_ids[dec_len++] = best_id;
    }

    // cleanup
    api->ReleaseValue(enc_input_ids);
    api->ReleaseValue(enc_attn_mask);
    api->ReleaseValue(hidden_state);
    free(mask_data);

    return out_len;
}

void inference_free(InferenceCtx* ctx) {
    const OrtApi* api = ctx->api;
    api->ReleaseSession(ctx->encoder);
    api->ReleaseSession(ctx->decoder);
    api->ReleaseEnv(ctx->env);
}
