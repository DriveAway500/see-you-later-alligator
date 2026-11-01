/*
 * Event-driven main loop. The process sleeps inside glfwWaitEvents() and is
 * woken by exactly three things:
 *   - a window/input event (GLFW callbacks run and set App::redraw),
 *   - the pty watcher thread, when the shell wrote something,
 *   - a timeout that only exists while there is something to schedule
 *     (the next cursor blink toggle, or the next allowed frame).
 * With the window idle and unfocused, or focused but past
 * cursor_blink_timeout, there is no timeout at all: 0% CPU.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "headers/app.h"
#include "headers/load_conf.h"
#include "headers/pty_watch.h"
#include "headers/window.h"

static GLFWwindow *g_win;                 /* for the title callback (no user data there) */

static void on_title(const char *title)   { if (g_win) glfwSetWindowTitle(g_win, title); }
static void wake_main(void)               { glfwPostEmptyEvent(); }

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
static void apply_actions(unsigned acts, App *a) {
    const Config *cfg = a->cfg;
    View *view = &a->view;

    int size = view->fs;
    if (acts & ACT_FONT_INCREASE) size += cfg->font_step;
    if (acts & ACT_FONT_DECREASE) size -= cfg->font_step;
    if (acts & ACT_FONT_RESET)    size  = cfg->font_size;

    if (size != view->fs && view_set_font_size(view, cfg, size))
        if (window_refit(a)) a->input.sel.active = false;

    if (acts & ACT_FULLSCREEN) window_toggle_fullscreen(a);
    a->redraw = true;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    Config cfg;
    int rc = load_config(&cfg, argc, argv);
    if (rc != 0) return rc < 0 ? 1 : 0;

    static App app;                               /* zero-initialised, big */
    app.cfg = &cfg;

    if (window_open(&app) != 0) return 1;
    g_win = app.win;

    if (view_init(&app.view, &cfg) != 0) {
        fprintf(stderr, "silly-term: cannot initialise the renderer\n");
        window_close(&app);
        return 1;
    }
    window_show(&app, cfg.cols, cfg.rows);

    if (term_init(&app.term, &cfg) != 0) {
        perror("silly-term: cannot start shell");
        view_free(&app.view);
        window_close(&app);
        return 1;
    }
    app.term.on_title = on_title;

    input_init(&app.input, &cfg);
    input_install(&app);

    PtyWatch watch;
    if (pty_watch_start(&watch, app.term.master, wake_main) != 0) {
        perror("silly-term: cannot start the pty watcher");
        term_free(&app.term);
        view_free(&app.view);
        window_close(&app);
        return 1;
    }

    const double min_dt = 1.0 / (double)cfg.fps;   /* frame cap; frames are only drawn on change */
    double last_draw    = -1.0e9;
    bool   drawn_blink  = true;

    app.redraw = true;                             /* first frame */

    while (!glfwWindowShouldClose(app.win)) {
        if (window_handle_resize(&app)) {
            app.input.sel.active = false;
            view_invalidate(&app.view);
            app.redraw = true;
        }

        const unsigned acts = input_take_actions(&app.input);
        if (acts) apply_actions(acts, &app);

        const int got = term_pump(&app.term);
        pty_watch_ack(&watch);                     /* re-arm: more data wakes us again */
        if (got < 0) break;                        /* shell exited */
        if (got > 0) {
            app.redraw = true;
            if (!app.input.sel.dragging) app.input.sel.active = false;
        }

        const double now = glfwGetTime();

        /* the cursor only changes by itself while it is visible and focused */
        const bool blinking = app.focused && app.term.cursor_visible && app.term.sb_off == 0;
        const bool blink_on = render_blink_on(&cfg, app.input.last_input, now);
        if (blinking && blink_on != drawn_blink) app.redraw = true;

        double timeout = -1.0;                     /* < 0: sleep until something happens */

        if (app.redraw && !app.iconified) {
            const double due = last_draw + min_dt;
            if (now >= due) {
                render_frame(&app.view, &cfg, &app.term, &app.input.sel, app.input.last_input,
                             app.focused, app.fbw, app.fbh, now);
                glfwSwapBuffers(app.win);
                drawn_blink = blink_on;
                last_draw   = now;
                app.redraw  = false;
            } else {
                timeout = due - now;               /* frame cap: come back when allowed */
            }
        }

        if (blinking) {
            const double next = render_next_blink(&cfg, app.input.last_input, now);
            if (next >= 0.0) {
                double d = next - now;
                if (d < 0.001) d = 0.001;
                if (timeout < 0.0 || d < timeout) timeout = d;
            }
        }

        if (timeout < 0.0) glfwWaitEvents();
        else               glfwWaitEventsTimeout(timeout);
    }

    pty_watch_stop(&watch);
    term_free(&app.term);
    view_free(&app.view);
    window_close(&app);
    return 0;
}
