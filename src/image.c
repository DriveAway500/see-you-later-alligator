#include "headers/image.h"

#include <stdio.h>
#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_NO_STDIO_UNUSED_WARNING
#include "headers/stb_image.h"
#pragma GCC diagnostic pop

uint8_t *image_load(const char *path, int *w, int *h) {
    char buf[1024];
    if (path[0] == '~' && path[1] == '/' && getenv("HOME")) {
        snprintf(buf, sizeof buf, "%s%s", getenv("HOME"), path + 1);
        path = buf;
    }
    int comp;
    return stbi_load(path, w, h, &comp, 4);       /* always RGBA */
}
