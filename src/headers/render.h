#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "load_conf.h"
#include "selection.h"
#include "term.h"

/* Font + cell metrics. */
typedef struct {
    Font font;
    int  fs;             /* font size in px */
    int  cw, ch;         /* cell width / height in px */
    int  cursor_style;   /* 0 block, 1 underline, 2 bar */
} View;

/* Call after InitWindow (needs a GL context). */
int  view_init(View *v, const Config *cfg);
void view_free(View *v);

/* Change the font size at runtime (clamped to 6..200). Returns 1 if it changed.
   The caller must refit the grid/window afterwards. */
int  view_set_font_size(View *v, const Config *cfg, int size);

/* Window size in px for the given grid. */
int  view_pixel_width(const View *v, const Config *cfg, int cols);
int  view_pixel_height(const View *v, const Config *cfg, int rows);

/* Draw one frame into the current BeginDrawing/EndDrawing pair. */
void render_frame(const View *v, const Config *cfg, const Term *t,
                  const Selection *sel, double last_input);

#endif