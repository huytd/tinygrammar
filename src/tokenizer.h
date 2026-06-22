#pragma once
#include <stddef.h>

#define MAX_VOCAB 32128
#define MAX_PIECE 32

typedef struct {
  char piece[MAX_PIECE];
  float score;
  int id;
} Token;

typedef struct {
  Token vocab[MAX_VOCAB];
  int vocab_size;
} Tokenizer;

int tokenizer_load(Tokenizer* tok, const char* json_path);
int tokenizer_encode(Tokenizer* tok, const char* text, int* out_ids, int max_ids);
void tokenizer_decode(Tokenizer* tok, const int* ids, int n, char* out, int max_out);
