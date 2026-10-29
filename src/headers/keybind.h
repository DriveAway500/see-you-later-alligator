#ifndef KEYBIND_H
#define KEYBIND_H

#include <stdbool.h>

typedef struct { int key; bool ctrl, shift, alt; } Bind;

#define BINDSET_MAX 4
/* Several alternatives for one action, e.g. "ctrl+equal, ctrl+kp_add". */
typedef struct { Bind b[BINDSET_MAX]; int n; } BindSet;

/* "ctrl+shift+c", "shift+pageup", "f11", ... (key == 0 if unparsable) */
Bind    parse_bind(const char *spec);

/* Comma-separated list of binds. Unparsable entries are dropped. */
BindSet parse_bindset(const char *spec);

/* True on the frame a key is pressed (or repeats) with exactly these modifiers held. */
bool    bind_hit(const Bind *b, bool ctrl, bool shift, bool alt);
bool    bindset_hit(const BindSet *s, bool ctrl, bool shift, bool alt);

/* Key pressed or auto-repeating this frame. */
bool    key_pressed(int raylib_key);

#endif