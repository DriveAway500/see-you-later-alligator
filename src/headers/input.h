#ifndef INPUT_H
#define INPUT_H

#include "keybind.h"
#include "load_conf.h"
#include "render.h"
#include "selection.h"
#include "term.h"

typedef struct {
    Bind      copy, paste, scroll_up, scroll_down;
    Selection sel;
    double    last_input;      /* time of last keypress (resets cursor blink) */
} Input;

void input_init(Input *in, const Config *cfg);

/* Keyboard -> shell (and the configurable shortcuts). Call once per frame. */
void input_keyboard(Input *in, Term *t);

/* Mouse: selection, middle-click paste, wheel scroll. Call once per frame. */
void input_mouse(Input *in, Term *t, const Config *cfg, const View *v);

#endif