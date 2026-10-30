#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>

#include "raylib.h"
#include "load_conf.h"
#include "selection.h"
#include "term.h"

/* Font, cell metrics and pre-resolved rendering colors. */
typedef struct {
    Font font;
    int  fs;             /* font size in px */
    int  cw, ch;         /* cell width / height in px */
    int  cursor_style;  /* 0 block, 1 underline, 2 bar */

    Color pal256[256];   /* 0..15 from Config, 16..255 from libvterm */
    Color col_fg;
    Color col_bg;
    Color col_sel_fg;
    Color col_sel_bg;
    Color col_cursor;

    /* glyph lookup, rebuilt whenever the font changes */
    int ascii_idx[128];  /* codepoint < 128 -> index in font.glyphs */
    int fallback_idx;    /* index of '?', used for missing glyphs */

    /* The VTerm palette needs Term::screen, which is not available in view_init. */
    const void *palette_screen;
    bool palette_ready;
} View;

/* Call after InitWindow (needs a GL context). */
int  view_init(View *v, const Config *cfg);
void view_free(View *v);

/* Cursor blink phase for the given time since the last input (true = visible).
   Always true when blink is disabled or cursor_blink_ms <= 0. */
bool render_blink_on(const Config *cfg, double last_input);

/*
 * Refresh cached colors and the indexed palette. Call after changing the
 * Config colors or the palette maintained by libvterm. render_frame also
 * performs a lazy initial palette fill and notices Config color changes.
 */
void view_update_colors(View *v, const Config *cfg, const Term *t);

/* Change the font size at runtime (clamped to 6..200). Returns 1 if it changed.
   The caller must refit the grid/window afterwards. */
int  view_set_font_size(View *v, const Config *cfg, int size);

/* Window size in px for the given grid. */
int  view_pixel_width(const View *v, const Config *cfg, int cols);
int  view_pixel_height(const View *v, const Config *cfg, int rows);

/* Draw one frame into the current BeginDrawing/EndDrawing pair.
   Pass NULL for sel when no selection is active. */
void render_frame(View *v, const Config *cfg, const Term *t,
                  const Selection *sel, double last_input);

#endif