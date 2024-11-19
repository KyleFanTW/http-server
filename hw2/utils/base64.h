#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);
unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length);
void build_decoding_table(void);
void base64_cleanup(void);

#endif