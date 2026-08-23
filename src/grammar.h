#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the grammar engine, loading the tokenizer and ONNX models.
// Returns 0 on success, non-zero on error.
int grammar_init(void);

// Fixes the grammar of the input text.
// Returns a heap-allocated string containing corrected text (caller must free),
// or NULL on failure.
char* grammar_fix(const char* input_text);

// Returns 1 if initialized, 0 otherwise.
int grammar_is_ready(void);

// Cleans up all loaded models and tokenizer.
void grammar_cleanup(void);

#ifdef __cplusplus
}
#endif
