#include "diff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static int is_punct_char(char c) {
    return (c == '.' || c == ',' || c == '!' || c == '?' ||
            c == ';' || c == ':' || c == '\'' || c == '"' ||
            c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '-' || c == '/' ||
            c == '\\' || c == '`' || c == '~' || c == '@' ||
            c == '#' || c == '$' || c == '%' || c == '^' ||
            c == '&' || c == '*' || c == '+' || c == '=' ||
            c == '<' || c == '>' || c == '|');
}

typedef struct {
    char** tokens;
    int count;
    int capacity;
} TokenList;

static void token_list_init(TokenList* list) {
    list->capacity = 64;
    list->count = 0;
    list->tokens = (char**)malloc(list->capacity * sizeof(char*));
}

static void token_list_add(TokenList* list, const char* str, int len) {
    if (len <= 0) return;
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->tokens = (char**)realloc(list->tokens, list->capacity * sizeof(char*));
    }
    char* t = (char*)malloc(len + 1);
    memcpy(t, str, len);
    t[len] = '\0';
    list->tokens[list->count++] = t;
}

static void token_list_free(TokenList* list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i]);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

static TokenList tokenize(const char* text) {
    TokenList list;
    token_list_init(&list);
    if (!text) return list;

    const char* p = text;
    while (*p) {
        if (is_space_char(*p)) {
            const char* start = p;
            while (*p && is_space_char(*p)) p++;
            token_list_add(&list, start, p - start);
        } else if (is_punct_char(*p)) {
            const char* start = p;
            char punct = *p;
            // Group consecutive identical punctuation like '...'
            while (*p == punct) p++;
            token_list_add(&list, start, p - start);
        } else {
            const char* start = p;
            while (*p && !is_space_char(*p) && !is_punct_char(*p)) {
                p++;
            }
            token_list_add(&list, start, p - start);
        }
    }

    return list;
}

static void diff_result_init(DiffResult* res) {
    res->capacity = 32;
    res->count = 0;
    res->chunks = (DiffChunk*)malloc(res->capacity * sizeof(DiffChunk));
}

static void diff_result_add(DiffResult* res, DiffOpType op, const char* text) {
    if (!text || strlen(text) == 0) return;
    if (res->count >= res->capacity) {
        res->capacity *= 2;
        res->chunks = (DiffChunk*)realloc(res->chunks, res->capacity * sizeof(DiffChunk));
    }
    res->chunks[res->count].op = op;
    res->chunks[res->count].text = strdup(text);
    res->count++;
}

void diff_result_free(DiffResult* res) {
    if (!res || !res->chunks) return;
    for (int i = 0; i < res->count; i++) {
        free(res->chunks[i].text);
    }
    free(res->chunks);
    res->chunks = NULL;
    res->count = 0;
    res->capacity = 0;
}

DiffResult diff_words(const char* original, const char* corrected) {
    DiffResult res;
    diff_result_init(&res);

    if (!original && !corrected) return res;
    if (!original || strlen(original) == 0) {
        if (corrected && strlen(corrected) > 0) {
            diff_result_add(&res, DIFF_OP_INSERT, corrected);
        }
        return res;
    }
    if (!corrected || strlen(corrected) == 0) {
        diff_result_add(&res, DIFF_OP_DELETE, original);
        return res;
    }

    if (strcmp(original, corrected) == 0) {
        diff_result_add(&res, DIFF_OP_EQUAL, original);
        return res;
    }

    TokenList A = tokenize(original);
    TokenList B = tokenize(corrected);

    int N = A.count;
    int M = B.count;

    if (N == 0 && M == 0) {
        token_list_free(&A);
        token_list_free(&B);
        return res;
    }
    if (N == 0) {
        diff_result_add(&res, DIFF_OP_INSERT, corrected);
        token_list_free(&A);
        token_list_free(&B);
        return res;
    }
    if (M == 0) {
        diff_result_add(&res, DIFF_OP_DELETE, original);
        token_list_free(&A);
        token_list_free(&B);
        return res;
    }

    // Allocate DP matrix
    int* dp = (int*)malloc((N + 1) * (M + 1) * sizeof(int));
    if (!dp) {
        diff_result_add(&res, DIFF_OP_EQUAL, corrected);
        token_list_free(&A);
        token_list_free(&B);
        return res;
    }

    #define DP(i, j) dp[(i) * (M + 1) + (j)]

    for (int i = 0; i <= N; i++) DP(i, 0) = 0;
    for (int j = 0; j <= M; j++) DP(0, j) = 0;

    for (int i = 1; i <= N; i++) {
        for (int j = 1; j <= M; j++) {
            if (strcmp(A.tokens[i - 1], B.tokens[j - 1]) == 0) {
                DP(i, j) = DP(i - 1, j - 1) + 1;
            } else {
                int top = DP(i - 1, j);
                int left = DP(i, j - 1);
                DP(i, j) = (top > left) ? top : left;
            }
        }
    }

    // Backtrack to collect raw ops in reverse
    typedef struct {
        DiffOpType op;
        const char* text;
    } RawOp;

    int max_ops = N + M + 4;
    RawOp* raw = (RawOp*)malloc(max_ops * sizeof(RawOp));
    int raw_count = 0;

    int i = N, j = M;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && strcmp(A.tokens[i - 1], B.tokens[j - 1]) == 0) {
            raw[raw_count].op = DIFF_OP_EQUAL;
            raw[raw_count].text = A.tokens[i - 1];
            raw_count++;
            i--; j--;
        } else if (j > 0 && (i == 0 || DP(i, j - 1) >= DP(i - 1, j))) {
            raw[raw_count].op = DIFF_OP_INSERT;
            raw[raw_count].text = B.tokens[j - 1];
            raw_count++;
            j--;
        } else {
            raw[raw_count].op = DIFF_OP_DELETE;
            raw[raw_count].text = A.tokens[i - 1];
            raw_count++;
            i--;
        }
    }

    #undef DP
    free(dp);

    // Merge consecutive tokens with the same op in forward order
    if (raw_count > 0) {
        int k = raw_count - 1;
        while (k >= 0) {
            DiffOpType current_op = raw[k].op;
            size_t total_len = 0;
            int start_k = k;

            while (k >= 0 && raw[k].op == current_op) {
                total_len += strlen(raw[k].text);
                k--;
            }

            char* merged = (char*)malloc(total_len + 1);
            merged[0] = '\0';
            for (int x = start_k; x > k; x--) {
                strcat(merged, raw[x].text);
            }

            diff_result_add(&res, current_op, merged);
            free(merged);
        }
    }

    free(raw);
    token_list_free(&A);
    token_list_free(&B);
    return res;
}

void diff_apply_to_buffer(GtkTextBuffer* buffer,
                          const DiffResult* diff,
                          GtkTextTag* tag_normal,
                          GtkTextTag* tag_delete,
                          GtkTextTag* tag_insert) {
    gtk_text_buffer_set_text(buffer, "", 0);

    for (int i = 0; i < diff->count; i++) {
        DiffOpType op = diff->chunks[i].op;
        const char* text = diff->chunks[i].text;
        if (!text || strlen(text) == 0) continue;

        GtkTextIter end_iter;
        gtk_text_buffer_get_end_iter(buffer, &end_iter);

        // If DELETE is immediately followed by INSERT, add a space between them if needed
        if (op == DIFF_OP_DELETE) {
            // Trim trailing spaces from deleted chunk to badge it cleanly
            size_t len = strlen(text);
            size_t r = len;
            while (r > 0 && is_space_char(text[r - 1])) r--;

            if (r > 0) {
                char* clean_del = (char*)malloc(r + 1);
                memcpy(clean_del, text, r);
                clean_del[r] = '\0';

                GtkTextIter match_start;
                gtk_text_buffer_get_end_iter(buffer, &match_start);
                gtk_text_buffer_insert_with_tags(buffer, &match_start, clean_del, -1, tag_delete, NULL);
                free(clean_del);
            }

            // If there's a following INSERT, or if trailing space existed, add normal space
            if (i + 1 < diff->count && diff->chunks[i + 1].op == DIFF_OP_INSERT) {
                GtkTextIter space_iter;
                gtk_text_buffer_get_end_iter(buffer, &space_iter);
                gtk_text_buffer_insert_with_tags(buffer, &space_iter, " ", 1, tag_normal, NULL);
            } else if (r < len) {
                GtkTextIter space_iter;
                gtk_text_buffer_get_end_iter(buffer, &space_iter);
                gtk_text_buffer_insert_with_tags(buffer, &space_iter, text + r, (gint)(len - r), tag_normal, NULL);
            }
        } else if (op == DIFF_OP_INSERT) {
            // Trim trailing spaces from inserted chunk
            size_t len = strlen(text);
            size_t r = len;
            while (r > 0 && is_space_char(text[r - 1])) r--;

            if (r > 0) {
                char* clean_ins = (char*)malloc(r + 1);
                memcpy(clean_ins, text, r);
                clean_ins[r] = '\0';

                GtkTextIter match_start;
                gtk_text_buffer_get_end_iter(buffer, &match_start);
                gtk_text_buffer_insert_with_tags(buffer, &match_start, clean_ins, -1, tag_insert, NULL);
                free(clean_ins);
            }

            if (r < len) {
                GtkTextIter space_iter;
                gtk_text_buffer_get_end_iter(buffer, &space_iter);
                gtk_text_buffer_insert_with_tags(buffer, &space_iter, text + r, (gint)(len - r), tag_normal, NULL);
            }
        } else {
            // Normal unchanged text
            GtkTextIter normal_iter;
            gtk_text_buffer_get_end_iter(buffer, &normal_iter);
            gtk_text_buffer_insert_with_tags(buffer, &normal_iter, text, -1, tag_normal, NULL);
        }
    }
}
