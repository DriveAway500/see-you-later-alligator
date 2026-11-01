#include "headers/keybind.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "headers/gl.h"   /* GLFW_KEY_* */

static int key_by_name(const char *t) {
    size_t n = strlen(t);

    if (n == 1) {
        if (isalpha((unsigned char)t[0])) return GLFW_KEY_A + (tolower((unsigned char)t[0]) - 'a');
        if (isdigit((unsigned char)t[0])) return GLFW_KEY_0 + (t[0] - '0');
    }
    if (t[0] == 'f' && n >= 2 && n <= 3 && isdigit((unsigned char)t[1])) {
        int f = atoi(t + 1);
        if (f >= 1 && f <= 12) return GLFW_KEY_F1 + f - 1;
    }

    static const struct { const char *name; int key; } names[] = {
        {"pageup", GLFW_KEY_PAGE_UP}, {"pagedown", GLFW_KEY_PAGE_DOWN},
        {"home", GLFW_KEY_HOME},      {"end", GLFW_KEY_END},
        {"insert", GLFW_KEY_INSERT},  {"delete", GLFW_KEY_DELETE},
        {"up", GLFW_KEY_UP},          {"down", GLFW_KEY_DOWN},
        {"left", GLFW_KEY_LEFT},      {"right", GLFW_KEY_RIGHT},
        {"space", GLFW_KEY_SPACE},    {"enter", GLFW_KEY_ENTER},
        {"tab", GLFW_KEY_TAB},        {"backspace", GLFW_KEY_BACKSPACE},
        {"equal", GLFW_KEY_EQUAL},    {"plus", GLFW_KEY_EQUAL},     /* '+' is shift+'=' on US layouts */
        {"minus", GLFW_KEY_MINUS},    {"comma", GLFW_KEY_COMMA},
        {"period", GLFW_KEY_PERIOD},  {"slash", GLFW_KEY_SLASH},
        {"kp_add", GLFW_KEY_KP_ADD},  {"kp_subtract", GLFW_KEY_KP_SUBTRACT},
        {"kp_0", GLFW_KEY_KP_0},
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

bool bind_hit(const Bind *b, int key, bool ctrl, bool shift, bool alt) {
    return b->key && b->key == key && b->ctrl == ctrl && b->shift == shift && b->alt == alt;
}

BindSet parse_bindset(const char *spec) {
    BindSet s = { .n = 0 };
    char buf[128];
    snprintf(buf, sizeof buf, "%s", spec);

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok && s.n < BINDSET_MAX; tok = strtok_r(NULL, ",", &save)) {
        Bind b = parse_bind(tok);
        if (b.key) s.b[s.n++] = b;
    }
    return s;
}

bool bindset_hit(const BindSet *s, int key, bool ctrl, bool shift, bool alt) {
    for (int i = 0; i < s->n; i++)
        if (bind_hit(&s->b[i], key, ctrl, shift, alt)) return true;
    return false;
}