#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* Decodes PNG/JPEG/BMP/GIF/TGA into tightly packed 8-bit RGBA (straight
   alpha, top row first). Returns malloc'd pixels (free()), or NULL. A leading
   "~/" in path is expanded to $HOME. */
uint8_t *image_load(const char *path, int *w, int *h);

#endif
