#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "gl.h"
#include "input.h"
#include "load_conf.h"
#include "render.h"
#include "term.h"

/* Everything the GLFW callbacks need to reach. */
typedef struct App {
    GLFWwindow *win;
    Config     *cfg;
    View        view;
    Term        term;
    Input       input;

    bool  redraw;            /* something visible changed: draw a frame */
    bool  resized;           /* framebuffer size changed: refit the grid */
    bool  focused;
    bool  iconified;

    int   fbw, fbh;          /* framebuffer size in px */
    float sx, sy;            /* framebuffer px per window unit (HiDPI) */

    int   win_x, win_y, win_w, win_h;   /* windowed geometry, saved while fullscreen */
} App;

#endif
