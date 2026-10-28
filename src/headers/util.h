#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

/* Encode one code point as UTF-8 into out (needs 4 bytes). Returns length. */
int utf8_encode(uint32_t cp, char *out);

#endif