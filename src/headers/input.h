#ifndef INPUT_H
#define INPUT_H

#include "keybind.h"
#include "load_conf.h"
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

    int       mods;              /* GLFW_MOD_* from the latest key/button event */
    bool      swallow_char;      /* the key that is about to produce a char was a shortcut */
    double    mx, my;            /* cursor position, window coordinates */
    double    wheel_lines;       /* fractional wheel/touchpad remainder (local scrolling) */
    double    wheel_steps;       /* same, for applications that asked for the mouse */
} Input;

void     input_init(Input *in, const Config *cfg);

/* Hooks the GLFW key/char/mouse callbacks of the window. The window's user
   pointer must be the App (see app.h). Everything is event driven: callbacks
   feed libvterm / the selection directly and set App::redraw when something
   visible changed. */
struct App;
void     input_install(struct App *app);

/* Returns and clears the pending Action bits. */
unsigned input_take_actions(Input *in);

#endif
