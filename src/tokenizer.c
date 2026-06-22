#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- Minimal JSON vocab parser ---
// Reads "vocab": [["piece", score], ...] from tokenizer.json
int tokenizer_load(Tokenizer* tok, const char* json_path) {
    FILE* f = fopen(json_path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    // Find "vocab": [
    char* p = strstr(buf, "\"vocab\"");
    p = strchr(p, '[');  // outer array
    p++;

    tok->vocab_size = 0;
    int id = 0;

    while (*p && *p != ']') {
        // skip to inner '['
        while (*p && *p != '[' && *p != ']') p++;
        if (*p == ']') break;
        p++;  // skip '['

        // parse piece string
        while (*p && *p != '"') p++;
        p++;  // skip opening quote
        char* dst = tok->vocab[id].piece;
        int pi = 0;
        while (*p && *p != '"' && pi < MAX_PIECE - 1) {
            // handle \u2581 (▁ = UTF-8 E2 96 81) — SentencePiece space marker
            if (p[0] == '\\' && p[1] == 'u') {
                // copy as-is for now; real impl should decode unicode
                dst[pi++] = ' ';  // map ▁ → space for simplicity
                p += 6;
            } else {
                dst[pi++] = *p++;
            }
        }
        dst[pi] = '\0';
        p++;  // skip closing quote

        // parse score
        while (*p && *p != ',' && *p != ']') p++;
        p++;  // skip ','
        tok->vocab[id].score = strtof(p, &p);
        tok->vocab[id].id = id;

        // skip to end of inner ']'
        while (*p && *p != ']') p++;
        p++;  // skip ']'
        id++;
    }

    tok->vocab_size = id;
    free(buf);
    return 0;
}

// normalize: prepend ▁ and replace spaces with ▁
static int normalize(const char* text, char* out, int max_out) {
    const char* spm_space = "\xe2\x96\x81";  // UTF-8 for ▁
    int i = 0;
    // prepend ▁
    while (*spm_space && i < max_out - 1) out[i++] = *spm_space++;
    // copy text, replacing ' ' with ▁
    for (const char* p = text; *p && i < max_out - 4; p++) {
        if (*p == ' ') {
            out[i++] = '\xe2';
            out[i++] = '\x96';
            out[i++] = '\x81';
        } else {
            out[i++] = *p;
        }
    }
    out[i] = '\0';
    return i;
}

int tokenizer_encode(Tokenizer* tok, const char* text, int* out_ids, int max_ids) {
    char normalized[4096];
    int n = normalize(text, normalized, sizeof(normalized));

    float* best_score = calloc(n + 1, sizeof(float));
    int*   best_tok   = calloc(n + 1, sizeof(int));
    int*   best_len   = calloc(n + 1, sizeof(int));

    for (int i = 0; i <= n; i++) best_score[i] = -1e38f;
    best_score[0] = 0.0f;

    for (int i = 0; i < n; i++) {
        if (best_score[i] < -1e37f) continue;
        for (int t = 0; t < tok->vocab_size; t++) {
            int plen = strlen(tok->vocab[t].piece);
            if (plen == 0) continue;
            if (strncmp(normalized + i, tok->vocab[t].piece, plen) == 0) {
                float s = best_score[i] + tok->vocab[t].score;
                if (s > best_score[i + plen]) {
                    best_score[i + plen] = s;
                    best_tok[i + plen]   = t;
                    best_len[i + plen]   = plen;
                }
            }
        }
    }

    // backtrack — guard against best_len[pos]==0
    int tmp[1024];
    int cnt = 0, pos = n;
    while (pos > 0 && cnt < max_ids) {
        int len = best_len[pos];
        if (len == 0) break;  // unresolved position, stop
        tmp[cnt++] = tok->vocab[best_tok[pos]].id;
        pos -= len;
    }

    // reverse
    for (int i = 0; i < cnt; i++) out_ids[i] = tmp[cnt - 1 - i];

    free(best_score); free(best_tok); free(best_len);
    return cnt;
}

void tokenizer_decode(Tokenizer* tok, const int* ids, int n, char* out, int max_out) {
    int i = 0;
    for (int t = 0; t < n; t++) {
        int id = ids[t];
        if (id < 0 || id >= tok->vocab_size) continue;

        const char* piece = tok->vocab[id].piece;

        // skip special tokens
        if (piece[0] == '<') continue;

        // copy piece, converting ▁ (E2 96 81) → space
        for (const char* p = piece; *p && i < max_out - 1; ) {
            if ((unsigned char)p[0] == 0xe2 &&
                (unsigned char)p[1] == 0x96 &&
                (unsigned char)p[2] == 0x81) {
                // ▁ at start of first token = no space (it's the prepended one)
                if (i > 0) out[i++] = ' ';
                p += 3;
            } else {
                out[i++] = *p++;
            }
        }
    }
    out[i] = '\0';
}
