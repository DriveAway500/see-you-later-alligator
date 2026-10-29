#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "headers/input.h"
#include "headers/load_conf.h"
#include "headers/render.h"
#include "headers/term.h"
#include "headers/window.h"

static void on_title(const char *title) { SetWindowTitle(title); }

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

    while (!WindowShouldClose()) {
        if (window_handle_resize(&view, &cfg, &term)) input.sel.active = false;

        input_keyboard(&input, &term);
        input_mouse(&input, &term, &cfg, &view);
        apply_actions(input_take_actions(&input), &view, &cfg, &term, &input);

        int got = term_pump(&term);
        if (got < 0) break;                                   /* shell exited */
        if (got > 0 && !input.sel.dragging) input.sel.active = false;

        BeginDrawing();
        render_frame(&view, &cfg, &term, &input.sel, input.last_input);
        EndDrawing();
    }

    term_free(&term);
    view_free(&view);
    CloseWindow();
    return 0;
}