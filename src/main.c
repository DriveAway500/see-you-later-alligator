#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "headers/input.h"
#include "headers/load_conf.h"
#include "headers/render.h"
#include "headers/term.h"
#include "headers/window.h"

#define IDLE_WAIT_S    0.008   /* poll interval when there is nothing to draw */
#define FORCE_REDRAW_S 0.5     /* safety redraw (expose without compositor) */

static void on_title(const char *title) { SetWindowTitle(title); }

/* Everything outside Term cells that changes what render_frame draws. */
typedef struct {
    bool   sel_active, sel_dragging;
    int    r0, c0, r1, c1;
    int    sb_off;
    bool   cursor_visible, focused, blink;
    double last_input;
} Snap;

static Snap snap_take(const Config *cfg, const Term *t, const Input *in) {
    Snap s;
    s.sel_active     = in->sel.active;
    s.sel_dragging   = in->sel.dragging;
    s.r0             = in->sel.r0;
    s.c0             = in->sel.c0;
    s.r1             = in->sel.r1;
    s.c1             = in->sel.c1;
    s.sb_off         = t->sb_off;
    s.cursor_visible = t->cursor_visible;
    s.focused        = IsWindowFocused();
    s.blink          = render_blink_on(cfg, in->last_input);
    s.last_input     = in->last_input;
    return s;
}

static bool snap_eq(const Snap *a, const Snap *b) {
    return a->sel_active == b->sel_active && a->sel_dragging == b->sel_dragging &&
           a->r0 == b->r0 && a->c0 == b->c0 && a->r1 == b->r1 && a->c1 == b->c1 &&
           a->sb_off == b->sb_off && a->cursor_visible == b->cursor_visible &&
           a->focused == b->focused && a->blink == b->blink &&
           a->last_input == b->last_input;
}

/* 0 = ok, 1 = exit successfully (help), -1 = usage error */
static int load_config(Config *cfg, int argc, char **argv) {
    const char *conf = NULL;
    bool explicit_conf = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            conf = argv[++i];
            explicit_conf = true;
        } else {
            fprintf(stderr, "usage: %s [-c config-file]\n", argv[0]);
            return strcmp(argv[i], "-h") ? -1 : 1;
        }
    }

    char path[512];
    if (!conf && default_conf_path(path, sizeof path) == 0) conf = path;

    if (!conf) {
        set_defaults(cfg);
    } else if (load_conf(conf, cfg) != 0 && explicit_conf) {
        fprintf(stderr, "silly-term: cannot open %s, using defaults\n", conf);
    }
    return 0;
}

/* Perform the window-level actions requested by input shortcuts. */
static void apply_actions(unsigned acts, View *view, const Config *cfg, Term *term, Input *in) {
    int size = view->fs;
    if (acts & ACT_FONT_INCREASE) size += cfg->font_step;
    if (acts & ACT_FONT_DECREASE) size -= cfg->font_step;
    if (acts & ACT_FONT_RESET)    size  = cfg->font_size;

    if (size != view->fs && view_set_font_size(view, cfg, size))
        if (window_refit(view, cfg, term)) in->sel.active = false;

    if (acts & ACT_FULLSCREEN) window_toggle_fullscreen();
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    Config cfg;
    int rc = load_config(&cfg, argc, argv);
    if (rc != 0) return rc < 0 ? 1 : 0;

    window_open(&cfg);

    View view;
    view_init(&view, &cfg);
    window_show(&view, &cfg, cfg.cols, cfg.rows);

    Term term;
    if (term_init(&term, &cfg) != 0) {
        perror("silly-term: cannot start shell");
        return 1;
    }
    term.on_title = on_title;

    Input input;
    input_init(&input, &cfg);

    bool   force = true;               /* first frame, and any event the snapshot can't see */
    double last_draw = 0.0;
    Snap   drawn = snap_take(&cfg, &term, &input);

    while (!WindowShouldClose()) {
        if (window_handle_resize(&view, &cfg, &term)) {
            input.sel.active = false;
            force = true;
        }

        input_keyboard(&input, &term);
        input_mouse(&input, &term, &cfg, &view);

        const unsigned acts = input_take_actions(&input);
        if (acts) {
            apply_actions(acts, &view, &cfg, &term, &input);
            force = true;
        }

        int got = term_pump(&term);
        if (got < 0) break;                                   /* shell exited */
        if (got > 0) {
            force = true;
            if (!input.sel.dragging) input.sel.active = false;
        }

        const bool hidden = IsWindowMinimized() || IsWindowHidden();
        if (hidden) force = true;                             /* redraw as soon as it is shown */

        const double now = GetTime();
        const Snap cur = snap_take(&cfg, &term, &input);

        if (!hidden && (force || !snap_eq(&cur, &drawn) || now - last_draw >= FORCE_REDRAW_S)) {
            BeginDrawing();
            render_frame(&view, &cfg, &term, &input.sel, input.last_input);
            EndDrawing();                                     /* swap + frame pacing + poll events */

            drawn = cur;
            last_draw = now;
            force = false;
        } else {
            /* idle: EndDrawing would poll events, so do it by hand and sleep */
            PollInputEvents();
            WaitTime(IDLE_WAIT_S);
        }
    }

    term_free(&term);
    view_free(&view);
    CloseWindow();
    return 0;
}