#include "headers/input.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

void input_init(Input *in, const Config *cfg) {
    memset(in, 0, sizeof *in);
    in->copy        = parse_bind(cfg->key_copy);
    in->paste       = parse_bind(cfg->key_paste);
    in->scroll_up   = parse_bind(cfg->key_scroll_up);
    in->scroll_down = parse_bind(cfg->key_scroll_down);
}

/* Any input that goes to the shell snaps the view back to the bottom. */
static void typed(Input *in, Term *t) {
    in->sel.active = false;
    t->sb_off      = 0;
    in->last_input = GetTime();
}

static void scroll(Input *in, Term *t, int lines) {
    term_scroll(t, lines);
    in->sel.active = false;
}

static void copy_selection(Input *in, const Term *t) {
    char *s = sel_text(&in->sel, t);
    if (!s) return;
    SetClipboardText(s);
    free(s);
}

static void paste(Term *t) {
    const char *txt = GetClipboardText();
    if (!txt || !*txt) return;

    size_t len = strlen(txt);
    char *b = malloc(len);
    if (!b) return;

    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (txt[i] == '\r' && txt[i + 1] == '\n') continue;
        b[n++] = (txt[i] == '\n') ? '\r' : txt[i];
    }
    vterm_keyboard_start_paste(t->vt);          /* emits ESC[200~ if the app asked for it */
    term_write(t, b, n);
    vterm_keyboard_end_paste(t->vt);
    free(b);
}

/* --------------------------------------------------------------- keyboard */

void input_keyboard(Input *in, Term *t) {
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT);
    VTermModifier mod = (VTermModifier)((shift ? VTERM_MOD_SHIFT : 0) |
                                        (alt   ? VTERM_MOD_ALT   : 0) |
                                        (ctrl  ? VTERM_MOD_CTRL  : 0));

    /* configurable shortcuts */
    bool bound = false;
    if      (bind_hit(&in->copy,        ctrl, shift, alt)) { copy_selection(in, t);        bound = true; }
    else if (bind_hit(&in->paste,       ctrl, shift, alt)) { typed(in, t); paste(t);       bound = true; }
    else if (bind_hit(&in->scroll_up,   ctrl, shift, alt)) { scroll(in, t,  t->rows / 2);  bound = true; }
    else if (bind_hit(&in->scroll_down, ctrl, shift, alt)) { scroll(in, t, -t->rows / 2);  bound = true; }

    /* printable characters (always drain the queue) */
    int ch;
    while ((ch = GetCharPressed())) {
        if (bound || ctrl) continue;
        typed(in, t);
        vterm_keyboard_unichar(t->vt, (uint32_t)ch, alt ? VTERM_MOD_ALT : VTERM_MOD_NONE);
    }
    if (bound) return;

    /* special keys */
    static const struct { int rl; VTermKey vk; } special[] = {
        {KEY_ENTER, VTERM_KEY_ENTER},   {KEY_KP_ENTER, VTERM_KEY_ENTER},
        {KEY_TAB, VTERM_KEY_TAB},       {KEY_BACKSPACE, VTERM_KEY_BACKSPACE},
        {KEY_ESCAPE, VTERM_KEY_ESCAPE},
        {KEY_UP, VTERM_KEY_UP},         {KEY_DOWN, VTERM_KEY_DOWN},
        {KEY_LEFT, VTERM_KEY_LEFT},     {KEY_RIGHT, VTERM_KEY_RIGHT},
        {KEY_INSERT, VTERM_KEY_INS},    {KEY_DELETE, VTERM_KEY_DEL},
        {KEY_HOME, VTERM_KEY_HOME},     {KEY_END, VTERM_KEY_END},
        {KEY_PAGE_UP, VTERM_KEY_PAGEUP},{KEY_PAGE_DOWN, VTERM_KEY_PAGEDOWN},
    };
    for (size_t i = 0; i < sizeof special / sizeof *special; i++)
        if (key_pressed(special[i].rl)) { typed(in, t); vterm_keyboard_key(t->vt, special[i].vk, mod); }

    for (int i = 0; i < 12; i++)
        if (key_pressed(KEY_F1 + i)) { typed(in, t); vterm_keyboard_key(t->vt, VTERM_KEY_FUNCTION(i + 1), mod); }

    /* ctrl+letter and a few punctuation keys -> control bytes */
    if (ctrl) {
        for (int k = KEY_A; k <= KEY_Z; k++) {
            if (!key_pressed(k)) continue;
            char seq[2] = { 0x1b, (char)(k - KEY_A + 1) };
            typed(in, t);
            if (alt) term_write(t, seq, 2); else term_write(t, seq + 1, 1);
        }
        static const struct { int rl; char byte; } ctl[] = {
            {KEY_SPACE, 0}, {KEY_LEFT_BRACKET, 27}, {KEY_BACKSLASH, 28}, {KEY_RIGHT_BRACKET, 29},
        };
        for (size_t i = 0; i < sizeof ctl / sizeof *ctl; i++)
            if (key_pressed(ctl[i].rl)) { typed(in, t); term_write(t, &ctl[i].byte, 1); }
    }
}

/* ------------------------------------------------------------------ mouse */

void input_mouse(Input *in, Term *t, const Config *cfg, const View *v) {
    Vector2 m = GetMousePosition();
    int c = (int)((m.x - cfg->padding) / v->cw);
    int r = (int)((m.y - cfg->padding) / v->ch);
    if (c < 0) c = 0;
    if (c >= t->cols) c = t->cols - 1;
    if (r < 0) r = 0;
    if (r >= t->rows) r = t->rows - 1;

    Selection *s = &in->sel;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s->active = s->dragging = true;
        s->r0 = s->r1 = r;
        s->c0 = s->c1 = c;
    } else if (s->dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        s->r1 = r;
        s->c1 = c;
    }
    if (s->dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        s->dragging = false;
        if (s->r0 == s->r1 && s->c0 == s->c1) s->active = false;      /* plain click */
        else if (cfg->copy_on_select)         copy_selection(in, t);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) { typed(in, t); paste(t); }

    float w = GetMouseWheelMove();
    if (w != 0.0f) {
        int n = (int)roundf(w * (float)cfg->scroll_lines);
        if (n == 0) n = w > 0 ? 1 : -1;

        if (t->altscreen) {                       /* vim, less, htop...: wheel -> arrows */
            for (int i = 0; i < abs(n); i++)
                vterm_keyboard_key(t->vt, n > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        } else {
            scroll(in, t, n);
        }
    }
}