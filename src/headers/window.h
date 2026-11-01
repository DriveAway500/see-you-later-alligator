#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include "app.h"

/* Init GLFW, create the (hidden) window + OpenGL 3.3 context, load GL and
   register the window callbacks. 0 on success. */
int  window_open(App *a);

/* Size the window for cols x rows, then show it. Call once after view_init. */
void window_show(App *a, int cols, int rows);

/* If the framebuffer size changed, recompute the grid and resize the
   terminal. Returns true if the grid size changed. */
bool window_handle_resize(App *a);

/* Call after the font size changed: refit the grid (free-size windows) or
   resize the window to keep the grid (fixed-size windows).
   Returns true if the grid size changed. */
bool window_refit(App *a);

void window_toggle_fullscreen(App *a);

void window_close(App *a);

#endif
