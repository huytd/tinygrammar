#pragma once

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DIFF_OP_EQUAL,
    DIFF_OP_DELETE,
    DIFF_OP_INSERT
} DiffOpType;

typedef struct {
    DiffOpType op;
    char* text;
} DiffChunk;

typedef struct {
    DiffChunk* chunks;
    int count;
    int capacity;
} DiffResult;

// Computes word-level diff between original and corrected text.
DiffResult diff_words(const char* original, const char* corrected);

// Frees memory allocated by diff_words.
void diff_result_free(DiffResult* res);

// Applies the diff result into a GtkTextBuffer using the provided tags.
void diff_apply_to_buffer(GtkTextBuffer* buffer,
                          const DiffResult* diff,
                          GtkTextTag* tag_normal,
                          GtkTextTag* tag_delete,
                          GtkTextTag* tag_insert);

#ifdef __cplusplus
}
#endif
