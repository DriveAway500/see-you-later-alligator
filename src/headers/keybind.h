#ifndef KEYBIND_H
#define KEYBIND_H

#include <stdbool.h>

typedef struct { int key; bool ctrl, shift, alt; } Bind;   /* key = GLFW_KEY_* */

#define BINDSET_MAX 4
/* Several alternatives for one action, e.g. "ctrl+equal, ctrl+kp_add". */
typedef struct { Bind b[BINDSET_MAX]; int n; } BindSet;

/* "ctrl+shift+c", "shift+pageup", "f11", ... (key == 0 if unparsable) */
Bind    parse_bind(const char *spec);

/* Comma-separated list of binds. Unparsable entries are dropped. */
BindSet parse_bindset(const char *spec);

/* True if `key` (a GLFW_KEY_* press/repeat event) with exactly these
   modifiers held matches the bind. */
bool    bind_hit(const Bind *b, int key, bool ctrl, bool shift, bool alt);
bool    bindset_hit(const BindSet *s, int key, bool ctrl, bool shift, bool alt);

#endif
