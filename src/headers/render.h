#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "glyph.h"
#include "load_conf.h"
#include "selection.h"
#include "term.h"

typedef struct { uint8_t r, g, b, a; } Rgba;

/* One instanced rectangle: 16 bytes, uploaded as-is. Either a solid colour
   (u == QUAD_SOLID) or a glyph cut out of the atlas (coverage * colour). */
#define QUAD_SOLID 0xFFFFu
typedef struct {
    int16_t  x, y, w, h;      /* destination, in framebuffer pixels */
    uint16_t u, v;            /* atlas texel of the top-left corner */
    Rgba     c;
} Quad;

typedef struct { Quad *q; int n, cap; } QBuf;

/* Quads of one terminal row. bg quads of the whole grid are drawn first,
   then every fg quad (glyphs, underline, strike), so glyphs that overflow
   their cell are never covered by the next row's background. */
typedef struct { QBuf bg, fg; } RowCache;

/*
 * Everything needed to draw: font + atlas, colours, GL objects, and a per-row
 * cache of quads. Only rows that libvterm reports as damaged (or whose
 * selection state changed) are rebuilt; the grid is drawn with ONE
 * instanced draw call and the cursor with a second, tiny one.
 */
typedef struct View {
    GlyphCache gc;
    int  fs;                 /* font size in px */
    int  cw, ch;             /* cell size in px */
    int  cursor_style;       /* 0 block, 1 underline, 2 bar */

    Rgba pal256[256];        /* 0..15 from Config, 16..255 from libvterm */
    Rgba col_fg, col_bg, col_sel_fg, col_sel_bg, col_cursor;
    const void *palette_screen;
    bool palette_ready;

    /* GL */
    unsigned prog, tex;
    unsigned vao[2], vbo[2];          /* [0] grid, [1] cursor overlay */
    size_t   vbo_cap[2];
    int      u_inv_view;
    int      tex_w, tex_h;

    /* background image (bg_tex == 0: none) */
    unsigned bg_prog, bg_tex, bg_vao;
    int      bg_w, bg_h, bg_mode;
    int      bg_u_rect, bg_u_uv, bg_u_inv, bg_u_alpha;

    /* row cache */
    RowCache *rows;
    int       nrows;
    Quad     *main_buf;
    size_t    main_cap;
    int       main_n;
    bool      main_stale;
    QBuf      ov;
    VTermScreenCell *row_buf;
    int       row_cap;

    /* what the cache was built from */
    bool      all_dirty;
    int       prev_off;
    Selection prev_sel;
    unsigned  atlas_epoch;
} View;

/* Call with the GL context current and gl_load() done. 0 on success. */
int  view_init(View *v, const Config *cfg);
void view_free(View *v);

/* Throw the row cache away (next frame rebuilds everything). */
void view_invalidate(View *v);

/* Cursor blink phase (true = visible). Always true when blinking is off,
   while typing, and after cursor_blink_timeout seconds without input. */
bool   render_blink_on(const Config *cfg, double last_input, double now);

/* Absolute time (glfwGetTime) of the next blink toggle, or < 0 if the cursor
   will not change by itself (blink disabled / stopped). */
double render_next_blink(const Config *cfg, double last_input, double now);

/* Refresh cached colours and the indexed palette. */
void view_update_colors(View *v, const Config *cfg, const Term *t);

/* Change the font size at runtime (clamped to 6..200). Returns 1 if it
   changed. The caller must refit the grid/window afterwards. */
int  view_set_font_size(View *v, const Config *cfg, int size);

/* Window size in px for the given grid. */
int  view_pixel_width(const View *v, const Config *cfg, int cols);
int  view_pixel_height(const View *v, const Config *cfg, int rows);

/* Draw one frame into the back buffer of the current context (the caller
   swaps). Consumes the damage recorded in t. sel may be NULL. */
void render_frame(View *v, const Config *cfg, Term *t, const Selection *sel,
                  double last_input, bool focused, int fbw, int fbh, double now);

#endif
