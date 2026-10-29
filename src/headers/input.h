#ifndef INPUT_H
#define INPUT_H

#include "keybind.h"
#include "load_conf.h"
#include "render.h"
#include "selection.h"
#include "term.h"

/* Actions that need the window/view; input reports them, main performs them. */
typedef enum {
    ACT_FONT_INCREASE = 1 << 0,
    ACT_FONT_DECREASE = 1 << 1,
    ACT_FONT_RESET    = 1 << 2,
    ACT_FULLSCREEN    = 1 << 3,
} Action;

typedef struct {
    BindSet   copy, paste, select_all;
    BindSet   scroll_up, scroll_down, scroll_top, scroll_bottom, clear_scrollback;
    BindSet   font_increase, font_decrease, font_reset, fullscreen;

    Selection sel;
    double    last_input;        /* time of last keypress (resets cursor blink) */
    unsigned  actions;           /* pending Action bits */
    int       mouse_r, mouse_c;  /* last cell reported to the application */
} Input;

void     input_init(Input *in, const Config *cfg);

/* Keyboard -> shell, plus the configurable shortcuts. Once per frame. */
void     input_keyboard(Input *in, Term *t);

/* Mouse: forwarded to the app when it asked for it (hold Shift to bypass),
   otherwise selection, middle-click paste and wheel scrolling. Once per frame. */
void     input_mouse(Input *in, Term *t, const Config *cfg, const View *v);

/* Returns and clears the pending Action bits. */
unsigned input_take_actions(Input *in);

#endif