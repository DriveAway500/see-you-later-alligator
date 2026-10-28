#include "headers/util.h"

int utf8_encode(uint32_t cp, char *o) {
    if (cp < 0x80)    { o[0] = (char)cp; return 1; }
    if (cp < 0x800)   { o[0] = (char)(0xC0 | (cp >> 6));  o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 63));
        o[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 63));
    o[2] = (char)(0x80 | ((cp >> 6) & 63));
    o[3] = (char)(0x80 | (cp & 63));
    return 4;
}