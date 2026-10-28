#include "headers/keybind.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

static int key_by_name(const char *t) {
    size_t n = strlen(t);

    if (n == 1) {
        if (isalpha((unsigned char)t[0])) return KEY_A + (tolower((unsigned char)t[0]) - 'a');
        if (isdigit((unsigned char)t[0])) return KEY_ZERO + (t[0] - '0');
    }
    if (t[0] == 'f' && n >= 2 && n <= 3 && isdigit((unsigned char)t[1])) {
        int f = atoi(t + 1);
        if (f >= 1 && f <= 12) return KEY_F1 + f - 1;
    }

    static const struct { const char *name; int key; } names[] = {
        {"pageup", KEY_PAGE_UP}, {"pagedown", KEY_PAGE_DOWN},
        {"home", KEY_HOME},      {"end", KEY_END},
        {"insert", KEY_INSERT},  {"delete", KEY_DELETE},
        {"up", KEY_UP},          {"down", KEY_DOWN},
        {"left", KEY_LEFT},      {"right", KEY_RIGHT},
        {"space", KEY_SPACE},    {"enter", KEY_ENTER},
        {"tab", KEY_TAB},
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(names[i].name, t)) return names[i].key;
    return 0;
}

Bind parse_bind(const char *spec) {
    Bind b = { 0 };
    char buf[64];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);

    for (char *t = strtok(buf, "+ "); t; t = strtok(NULL, "+ ")) {
        if      (!strcmp(t, "ctrl") || !strcmp(t, "control")) b.ctrl = true;
        else if (!strcmp(t, "shift")) b.shift = true;
        else if (!strcmp(t, "alt"))   b.alt = true;
        else                          b.key = key_by_name(t);
    }
    return b;
}

bool key_pressed(int k) {
    return IsKeyPressed(k) || IsKeyPressedRepeat(k);
}

bool bind_hit(const Bind *b, bool ctrl, bool shift, bool alt) {
    return b->key && b->ctrl == ctrl && b->shift == shift && b->alt == alt && key_pressed(b->key);
}