#ifndef GLYPH_H
#define GLYPH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "load_conf.h"

/* Where a glyph lives in the atlas and where to draw it, relative to the
   top-left corner of its cell. w == 0 means "nothing to draw". */
typedef struct {
    int16_t  x, y;
    uint16_t w, h;
    uint16_t u, v;          /* atlas position, in texels */
} Glyph;

typedef struct { uint32_t cp; Glyph g; } GlyphSlot;

#define GLYPH_MAX_FACES 12

/*
 * Font (FreeType) + glyph atlas (8-bit coverage, CPU copy). Glyphs are
 * rasterised lazily the first time a codepoint is drawn, and missing glyphs
 * fall back to the next installed font. No GL calls in here: the renderer
 * uploads the dirty rows of `pix` to a texture.
 */
typedef struct {
    FT_Library ft;
    FT_Face    faces[GLYPH_MAX_FACES];
    char      *paths[GLYPH_MAX_FACES];   /* fallbacks are opened on first miss */
    bool       tried[GLYPH_MAX_FACES];
    int        nfaces;

    int   px;                            /* font size in px */
    bool  smooth;                        /* false = 1-bit (pixel) rendering */
    int   cw, ch;                        /* cell size */
    int   baseline;                      /* from the top of the cell */

    uint8_t *pix;                        /* aw * ah texels */
    int      aw, ah;
    int      pen_x, pen_y, row_h;
    int      dirty_y0, dirty_y1;         /* texel rows to upload; y0 >= y1 = clean */
    bool     resized;                    /* texture must be re-created */
    unsigned epoch;                      /* bumped when the atlas is cleared */

    GlyphSlot *tab;                      /* open-addressing hash, cp -> Glyph */
    size_t     tab_cap, tab_n;
} GlyphCache;

int   glyphs_init(GlyphCache *g, const Config *cfg, int px);
void  glyphs_free(GlyphCache *g);

/* Change the size, keeping the opened fonts. Clears the atlas. 0 on success. */
int   glyphs_set_size(GlyphCache *g, const Config *cfg, int px);

Glyph glyphs_get(GlyphCache *g, uint32_t cp);

/* Returns the rows to upload (and clears the dirty state). */
bool  glyphs_take_dirty(GlyphCache *g, int *y0, int *y1);

#endif
