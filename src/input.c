#include "headers/input.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

void input_init(Input *in, const Config *cfg) {
    memset(in, 0, sizeof *in);
    in->copy             = parse_bindset(cfg->key_copy);
    in->paste            = parse_bindset(cfg->key_paste);
    in->select_all       = parse_bindset(cfg->key_select_all);
    in->scroll_up        = parse_bindset(cfg->key_scroll_up);
    in->scroll_down      = parse_bindset(cfg->key_scroll_down);
    in->scroll_top       = parse_bindset(cfg->key_scroll_top);
    in->scroll_bottom    = parse_bindset(cfg->key_scroll_bottom);
    in->clear_scrollback = parse_bindset(cfg->key_clear_scrollback);
    in->font_increase    = parse_bindset(cfg->key_font_increase);
    in->font_decrease    = parse_bindset(cfg->key_font_decrease);
    in->font_reset       = parse_bindset(cfg->key_font_reset);
    in->fullscreen       = parse_bindset(cfg->key_fullscreen);
    in->mouse_r = in->mouse_c = -1;
}

unsigned input_take_actions(Input *in) {
    unsigned a = in->actions;
    in->actions = 0;
    return a;
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

static void select_all(Input *in, const Term *t) {
    in->sel.active = true;
    in->sel.r0 = 0;            in->sel.c0 = 0;
    in->sel.r1 = t->rows - 1;  in->sel.c1 = t->cols - 1;
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

static VTermModifier modifiers(bool ctrl, bool shift, bool alt) {
    return (VTermModifier)((shift ? VTERM_MOD_SHIFT : 0) |
                           (alt   ? VTERM_MOD_ALT   : 0) |
                           (ctrl  ? VTERM_MOD_CTRL  : 0));
}

/* --------------------------------------------------------------- keyboard */

/* Runs the configurable shortcuts. Returns true if one fired. */
static bool run_shortcuts(Input *in, Term *t, bool ctrl, bool shift, bool alt) {
#define HIT(set) bindset_hit(&in->set, ctrl, shift, alt)
    if      (HIT(copy))             copy_selection(in, t);
    else if (HIT(paste))          { typed(in, t); paste(t); }
    else if (HIT(select_all))       select_all(in, t);
    else if (HIT(scroll_up))        scroll(in, t,  t->rows / 2);
    else if (HIT(scroll_down))      scroll(in, t, -t->rows / 2);
    else if (HIT(scroll_top))       scroll(in, t,  t->sb_count);
    else if (HIT(scroll_bottom))    scroll(in, t, -t->sb_off);
    else if (HIT(clear_scrollback)) { term_clear_scrollback(t); in->sel.active = false; }
    else if (HIT(font_increase))    in->actions |= ACT_FONT_INCREASE;
    else if (HIT(font_decrease))    in->actions |= ACT_FONT_DECREASE;
    else if (HIT(font_reset))       in->actions |= ACT_FONT_RESET;
    else if (HIT(fullscreen))       in->actions |= ACT_FULLSCREEN;
    else return false;
#undef HIT
    return true;
}

void input_keyboard(Input *in, Term *t) {
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT);
    VTermModifier mod = modifiers(ctrl, shift, alt);

    bool bound = run_shortcuts(in, t, ctrl, shift, alt);

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

/* Mouse events for applications (vim, tmux, htop, mc...). libvterm encodes them
   in whichever protocol the app negotiated (X10 / SGR / urxvt). */
static void mouse_report(Input *in, Term *t, int r, int c, VTermModifier mod) {
    in->sel.active = in->sel.dragging = false;
    if (t->sb_off > 0) t->sb_off = 0;

    if (r != in->mouse_r || c != in->mouse_c) {
        vterm_mouse_move(t->vt, r, c, mod);
        in->mouse_r = r;
        in->mouse_c = c;
    }

    static const struct { int rl; int vt; } buttons[] = {
        {MOUSE_BUTTON_LEFT, 1}, {MOUSE_BUTTON_MIDDLE, 2}, {MOUSE_BUTTON_RIGHT, 3},
    };
    for (size_t i = 0; i < sizeof buttons / sizeof *buttons; i++) {
        if (IsMouseButtonPressed(buttons[i].rl))  vterm_mouse_button(t->vt, buttons[i].vt, true,  mod);
        if (IsMouseButtonReleased(buttons[i].rl)) vterm_mouse_button(t->vt, buttons[i].vt, false, mod);
    }

    float w = GetMouseWheelMove();
    if (w != 0.0f) {
        int steps = (int)ceilf(fabsf(w));
        for (int i = 0; i < steps; i++)
            vterm_mouse_button(t->vt, w > 0 ? 4 : 5, true, mod);     /* 4 = wheel up, 5 = wheel down */
    }
}

/* Local behaviour: selection, middle-click paste, wheel scrolling. */
static void mouse_local(Input *in, Term *t, const Config *cfg, int r, int c) {
    in->mouse_r = in->mouse_c = -1;

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

        if (t->altscreen) {                       /* less, man...: wheel -> arrows */
            for (int i = 0; i < abs(n); i++)
                vterm_keyboard_key(t->vt, n > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        } else {
            scroll(in, t, n);
        }
    }
}

void input_mouse(Input *in, Term *t, const Config *cfg, const View *v) {
    Vector2 m = GetMousePosition();
    int c = (int)((m.x - cfg->padding) / v->cw);
    int r = (int)((m.y - cfg->padding) / v->ch);
    if (c < 0) c = 0;
    if (c >= t->cols) c = t->cols - 1;
    if (r < 0) r = 0;
    if (r >= t->rows) r = t->rows - 1;

    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT);

    /* Shift always bypasses the application, so text stays selectable in vim/tmux. */
    bool report = cfg->mouse_reporting && t->mouse_mode != VTERM_PROP_MOUSE_NONE && !shift;

    if (report) mouse_report(in, t, r, c, modifiers(ctrl, false, alt));
    else        mouse_local(in, t, cfg, r, c);
}