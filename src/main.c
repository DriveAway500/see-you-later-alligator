#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "headers/input.h"
#include "headers/load_conf.h"
#include "headers/render.h"
#include "headers/term.h"

static void on_title(const char *title) { SetWindowTitle(title); }

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

static void open_window(const Config *cfg) {
    unsigned flags = FLAG_WINDOW_HIDDEN;        /* hidden until we know the real size */
    if (!cfg->decorated)    flags |= FLAG_WINDOW_UNDECORATED;
    if (cfg->resizable)     flags |= FLAG_WINDOW_RESIZABLE;
    if (cfg->opacity < 1.f) flags |= FLAG_WINDOW_TRANSPARENT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(100, 100, cfg->title);
    SetExitKey(KEY_NULL);                       /* ESC must reach the shell */
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    Config cfg;
    int rc = load_config(&cfg, argc, argv);
    if (rc != 0) return rc < 0 ? 1 : 0;

    open_window(&cfg);

    View view;
    view_init(&view, &cfg);

    SetWindowSize(view_pixel_width(&view, &cfg, cfg.cols), view_pixel_height(&view, &cfg, cfg.rows));
    if (cfg.resizable)
        SetWindowMinSize(view_pixel_width(&view, &cfg, 2), view_pixel_height(&view, &cfg, 1));
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetTargetFPS(cfg.fps);

    Term term;
    if (term_init(&term, &cfg) != 0) {
        perror("silly-term: cannot start shell");
        return 1;
    }
    term.on_title = on_title;

    Input input;
    input_init(&input, &cfg);

    while (!WindowShouldClose()) {
        if (cfg.resizable && IsWindowResized()) {
            int nc = (GetScreenWidth()  - 2 * cfg.padding) / view.cw;
            int nr = (GetScreenHeight() - 2 * cfg.padding) / view.ch;
            if (nc < 1) nc = 1;
            if (nr < 1) nr = 1;
            if (nc != term.cols || nr != term.rows) {
                term_resize(&term, nc, nr);
                input.sel.active = false;
            }
        }

        input_keyboard(&input, &term);
        input_mouse(&input, &term, &cfg, &view);

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