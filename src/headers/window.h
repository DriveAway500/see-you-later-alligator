#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include "load_conf.h"
#include "render.h"
#include "term.h"

/* Create the (hidden) window and apply the config flags. */
void window_open(const Config *cfg);

/* Size the window for cols x rows, then show it. Call once after view_init. */
void window_show(const View *v, const Config *cfg, int cols, int rows);

/* If the user resized the window, recompute the grid and resize the terminal.
   Returns true if the grid size changed. */
bool window_handle_resize(const View *v, const Config *cfg, Term *t);

/* Call after the font size changed: refit the grid (free-size windows) or
   resize the window to keep the grid (fixed-size windows).
   Returns true if the grid size changed. */
bool window_refit(const View *v, const Config *cfg, Term *t);

void window_toggle_fullscreen(void);

#endif