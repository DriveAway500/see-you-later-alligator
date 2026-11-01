#include "headers/input.h"
#include "headers/app.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
    in->last_input = glfwGetTime();
}

unsigned input_take_actions(Input *in) {
    unsigned a = in->actions;
    in->actions = 0;
    return a;
}

static inline App *app_of(GLFWwindow *w) { return glfwGetWindowUserPointer(w); }

static VTermModifier modifiers(bool ctrl, bool shift, bool alt) {
    return (VTermModifier)((shift ? VTERM_MOD_SHIFT : 0) |
                           (alt   ? VTERM_MOD_ALT   : 0) |
                           (ctrl  ? VTERM_MOD_CTRL  : 0));
}

/* Any input that goes to the shell snaps the view back to the bottom. */
static void typed(App *a) {
    Input *in = &a->input;
    if (in->sel.active || a->term.sb_off != 0) a->redraw = true;
    in->sel.active  = false;
    a->term.sb_off  = 0;
    in->last_input  = glfwGetTime();
}

static void scroll(App *a, int lines) {
    term_scroll(&a->term, lines);
    a->input.sel.active = false;
    a->redraw = true;
}

static void copy_selection(App *a) {
    char *s = sel_text(&a->input.sel, &a->term);
    if (!s) return;
    glfwSetClipboardString(NULL, s);
    free(s);
}

static void select_all(App *a) {
    Selection *s = &a->input.sel;
    s->active = true;
    s->r0 = 0;                  s->c0 = 0;
    s->r1 = a->term.rows - 1;   s->c1 = a->term.cols - 1;
    a->redraw = true;
}

static void paste(Term *t) {
    const char *txt = glfwGetClipboardString(NULL);
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

/* Runs the configurable shortcuts. Returns true if one fired. */
static bool run_shortcuts(App *a, int key, bool ctrl, bool shift, bool alt) {
    Input *in = &a->input;
    Term  *t  = &a->term;
#define HIT(set) bindset_hit(&in->set, key, ctrl, shift, alt)
    if      (HIT(copy))             copy_selection(a);
    else if (HIT(paste))          { typed(a); paste(t); }
    else if (HIT(select_all))       select_all(a);
    else if (HIT(scroll_up))        scroll(a, t->rows / 2);
    else if (HIT(scroll_down))      scroll(a, -t->rows / 2);
    else if (HIT(scroll_top))       scroll(a, t->sb_count);
    else if (HIT(scroll_bottom))    scroll(a, -t->sb_off);
    else if (HIT(clear_scrollback)) { term_clear_scrollback(t); in->sel.active = false; a->redraw = true; }
    else if (HIT(font_increase))    in->actions |= ACT_FONT_INCREASE;
    else if (HIT(font_decrease))    in->actions |= ACT_FONT_DECREASE;
    else if (HIT(font_reset))       in->actions |= ACT_FONT_RESET;
    else if (HIT(fullscreen))       in->actions |= ACT_FULLSCREEN;
    else return false;
#undef HIT
    return true;
}

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods) {
    (void)scancode;
    App *a = app_of(w);
    Input *in = &a->input;
    Term *t = &a->term;

    in->mods = mods;
    if (action == GLFW_RELEASE || key == GLFW_KEY_UNKNOWN) return;

    const bool ctrl  = mods & GLFW_MOD_CONTROL;
    const bool shift = mods & GLFW_MOD_SHIFT;
    const bool alt   = mods & GLFW_MOD_ALT;

    const bool bound = run_shortcuts(a, key, ctrl, shift, alt);
    /* GLFW sends the matching char event right after this key event */
    in->swallow_char = bound || ctrl;
    if (bound) return;

    const VTermModifier mod = modifiers(ctrl, shift, alt);

    static const struct { int glfw; VTermKey vk; } special[] = {
        {GLFW_KEY_ENTER, VTERM_KEY_ENTER},   {GLFW_KEY_KP_ENTER, VTERM_KEY_ENTER},
        {GLFW_KEY_TAB, VTERM_KEY_TAB},       {GLFW_KEY_BACKSPACE, VTERM_KEY_BACKSPACE},
        {GLFW_KEY_ESCAPE, VTERM_KEY_ESCAPE},
        {GLFW_KEY_UP, VTERM_KEY_UP},         {GLFW_KEY_DOWN, VTERM_KEY_DOWN},
        {GLFW_KEY_LEFT, VTERM_KEY_LEFT},     {GLFW_KEY_RIGHT, VTERM_KEY_RIGHT},
        {GLFW_KEY_INSERT, VTERM_KEY_INS},    {GLFW_KEY_DELETE, VTERM_KEY_DEL},
        {GLFW_KEY_HOME, VTERM_KEY_HOME},     {GLFW_KEY_END, VTERM_KEY_END},
        {GLFW_KEY_PAGE_UP, VTERM_KEY_PAGEUP},{GLFW_KEY_PAGE_DOWN, VTERM_KEY_PAGEDOWN},
    };
    for (size_t i = 0; i < sizeof special / sizeof *special; i++)
        if (special[i].glfw == key) {
            typed(a);
            vterm_keyboard_key(t->vt, special[i].vk, mod);
            return;
        }

    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        typed(a);
        vterm_keyboard_key(t->vt, VTERM_KEY_FUNCTION(key - GLFW_KEY_F1 + 1), mod);
        return;
    }

    /* ctrl+letter and a few punctuation keys -> control bytes */
    if (!ctrl) return;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        char seq[2] = { 0x1b, (char)(key - GLFW_KEY_A + 1) };
        typed(a);
        if (alt) term_write(t, seq, 2); else term_write(t, seq + 1, 1);
        return;
    }
    static const struct { int glfw; char byte; } ctl[] = {
        {GLFW_KEY_SPACE, 0}, {GLFW_KEY_LEFT_BRACKET, 27},
        {GLFW_KEY_BACKSLASH, 28}, {GLFW_KEY_RIGHT_BRACKET, 29},
    };
    for (size_t i = 0; i < sizeof ctl / sizeof *ctl; i++)
        if (ctl[i].glfw == key) { typed(a); term_write(t, &ctl[i].byte, 1); return; }
}

static void on_char(GLFWwindow *w, unsigned int cp) {
    App *a = app_of(w);
    Input *in = &a->input;
    if (in->swallow_char) return;
    typed(a);
    vterm_keyboard_unichar(a->term.vt, (uint32_t)cp,
                           (in->mods & GLFW_MOD_ALT) ? VTERM_MOD_ALT : VTERM_MOD_NONE);
}

/* ------------------------------------------------------------------ mouse */

static void cell_at(const App *a, int *r, int *c) {
    const Config *cfg = a->cfg;
    const double px = a->input.mx * (double)a->sx;
    const double py = a->input.my * (double)a->sy;
    int cc = (int)floor((px - cfg->padding) / (double)a->view.cw);
    int rr = (int)floor((py - cfg->padding) / (double)a->view.ch);
    if (cc < 0) cc = 0;
    if (cc >= a->term.cols) cc = a->term.cols - 1;
    if (rr < 0) rr = 0;
    if (rr >= a->term.rows) rr = a->term.rows - 1;
    *r = rr;
    *c = cc;
}

/* Shift always bypasses the application, so text stays selectable in vim/tmux. */
static bool reporting(const App *a, int mods) {
    return a->cfg->mouse_reporting && a->term.mouse_mode != VTERM_PROP_MOUSE_NONE &&
           !(mods & GLFW_MOD_SHIFT);
}

static VTermModifier mouse_mod(int mods) {
    return modifiers(mods & GLFW_MOD_CONTROL, false, mods & GLFW_MOD_ALT);
}

/* Events for applications (vim, tmux, htop, mc...). libvterm encodes them in
   whichever protocol the app negotiated (X10 / SGR / urxvt). */
static void report_move(App *a, int r, int c, int mods) {
    Input *in = &a->input;
    if (r == in->mouse_r && c == in->mouse_c) return;
    vterm_mouse_move(a->term.vt, r, c, mouse_mod(mods));
    in->mouse_r = r;
    in->mouse_c = c;
}

static void report_begin(App *a) {
    a->input.sel.active = a->input.sel.dragging = false;
    if (a->term.sb_off > 0) { a->term.sb_off = 0; a->redraw = true; }
}

static void on_cursor(GLFWwindow *w, double x, double y) {
    App *a = app_of(w);
    Input *in = &a->input;
    in->mx = x;
    in->my = y;

    int r, c;
    cell_at(a, &r, &c);

    if (reporting(a, in->mods)) { report_move(a, r, c, in->mods); return; }

    in->mouse_r = in->mouse_c = -1;
    Selection *s = &in->sel;
    if (s->dragging && (r != s->r1 || c != s->c1)) {
        s->r1 = r;
        s->c1 = c;
        a->redraw = true;
    }
}

static void on_button(GLFWwindow *w, int button, int action, int mods) {
    App *a = app_of(w);
    Input *in = &a->input;
    Term *t = &a->term;
    in->mods = mods;

    int r, c;
    cell_at(a, &r, &c);

    if (reporting(a, mods)) {
        report_begin(a);
        report_move(a, r, c, mods);
        int vb = button == GLFW_MOUSE_BUTTON_LEFT   ? 1
               : button == GLFW_MOUSE_BUTTON_MIDDLE ? 2
               : button == GLFW_MOUSE_BUTTON_RIGHT  ? 3 : 0;
        if (vb) vterm_mouse_button(t->vt, vb, action == GLFW_PRESS, mouse_mod(mods));
        return;
    }

    /* local behaviour: selection and middle-click paste */
    in->mouse_r = in->mouse_c = -1;
    Selection *s = &in->sel;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            s->active = s->dragging = true;
            s->r0 = s->r1 = r;
            s->c0 = s->c1 = c;
            a->redraw = true;
        } else if (s->dragging) {
            s->dragging = false;
            s->r1 = r;
            s->c1 = c;
            if (s->r0 == s->r1 && s->c0 == s->c1) s->active = false;     /* plain click */
            else if (a->cfg->copy_on_select)      copy_selection(a);
            a->redraw = true;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        typed(a);
        paste(t);
    }
}

static void on_scroll(GLFWwindow *w, double xoff, double yoff) {
    (void)xoff;
    if (yoff == 0.0) return;
    App *a = app_of(w);
    Input *in = &a->input;
    Term *t = &a->term;

    int r, c;
    cell_at(a, &r, &c);

    if (reporting(a, in->mods)) {
        report_begin(a);
        report_move(a, r, c, in->mods);
        in->wheel_steps += yoff;
        while (in->wheel_steps >= 1.0)  { vterm_mouse_button(t->vt, 4, true, mouse_mod(in->mods)); in->wheel_steps -= 1.0; }
        while (in->wheel_steps <= -1.0) { vterm_mouse_button(t->vt, 5, true, mouse_mod(in->mods)); in->wheel_steps += 1.0; }
        return;
    }

    /* touchpads send fractions: accumulate until at least one line */
    in->wheel_lines += yoff * (double)a->cfg->scroll_lines;
    int n = (int)in->wheel_lines;
    in->wheel_lines -= n;
    if (n == 0) return;

    if (t->altscreen) {                           /* less, man...: wheel -> arrows */
        for (int i = 0; i < abs(n); i++)
            vterm_keyboard_key(t->vt, n > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
    } else {
        scroll(a, n);
    }
}

void input_install(App *a) {
    glfwGetCursorPos(a->win, &a->input.mx, &a->input.my);
    glfwSetKeyCallback(a->win, on_key);
    glfwSetCharCallback(a->win, on_char);
    glfwSetCursorPosCallback(a->win, on_cursor);
    glfwSetMouseButtonCallback(a->win, on_button);
    glfwSetScrollCallback(a->win, on_scroll);
}
