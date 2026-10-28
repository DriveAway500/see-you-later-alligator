#ifndef KEYBIND_H
#define KEYBIND_H

#include <stdbool.h>

typedef struct { int key; bool ctrl, shift, alt; } Bind;

/* "ctrl+shift+c", "shift+pageup", "f11", ... (key == 0 if unparsable) */
Bind parse_bind(const char *spec);

/* True on the frame the key is pressed (or repeats) with exactly these modifiers held. */
bool bind_hit(const Bind *b, bool ctrl, bool shift, bool alt);

/* Key pressed or auto-repeating this frame. */
bool key_pressed(int raylib_key);

#endif