#include <stdio.h>
#include "tokenizer.h"

int main() {
    Tokenizer tok;
    if (tokenizer_load(&tok, "model/tokenizer.json") != 0) {
        fprintf(stderr, "Failed to load tokenizer\n");
        return 1;
    }
    printf("Vocab size: %d\n", tok.vocab_size);

    const char* text = " He go to school yesterday.";  // intentional grammar error
    int ids[256];
    int n = tokenizer_encode(&tok, text, ids, 256);

    printf("Token count: %d\n", n);
    for (int i = 0; i < n; i++) {
        printf("  [%d] id=%d  piece='%s'\n", i, ids[i], tok.vocab[ids[i]].piece);
    }

    char decoded[1024];
    tokenizer_decode(&tok, ids, n, decoded, sizeof(decoded));
    printf("Decoded: %s\n", decoded);

    return 0;
}
