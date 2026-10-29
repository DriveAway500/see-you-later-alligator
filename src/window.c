#include "headers/window.h"

#include "raylib.h"

/* Windows we may resize the grid for: resizable ones, or any window while
   it is in (borderless) fullscreen. */
static bool grid_follows_window(const Config *cfg) {
    return cfg->resizable || IsWindowFullscreen() || IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
}

static bool fit_grid(const View *v, const Config *cfg, Term *t) {
    int nc = (GetScreenWidth()  - 2 * cfg->padding) / v->cw;
    int nr = (GetScreenHeight() - 2 * cfg->padding) / v->ch;
    if (nc < 1) nc = 1;
    if (nr < 1) nr = 1;
    if (nc == t->cols && nr == t->rows) return false;
    term_resize(t, nc, nr);
    return true;
}

void window_open(const Config *cfg) {
    unsigned flags = FLAG_WINDOW_HIDDEN;           /* hidden until we know the real size */
    if (!cfg->decorated)    flags |= FLAG_WINDOW_UNDECORATED;
    if (cfg->resizable)     flags |= FLAG_WINDOW_RESIZABLE;
    if (cfg->opacity < 1.f) flags |= FLAG_WINDOW_TRANSPARENT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(100, 100, cfg->title);
    SetExitKey(KEY_NULL);                          /* ESC must reach the shell */
}

void window_show(const View *v, const Config *cfg, int cols, int rows) {
    SetWindowSize(view_pixel_width(v, cfg, cols), view_pixel_height(v, cfg, rows));
    if (cfg->resizable)
        SetWindowMinSize(view_pixel_width(v, cfg, 2), view_pixel_height(v, cfg, 1));
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetTargetFPS(cfg->fps);
}

bool window_handle_resize(const View *v, const Config *cfg, Term *t) {
    if (!IsWindowResized() || !grid_follows_window(cfg)) return false;
    return fit_grid(v, cfg, t);
}

bool window_refit(const View *v, const Config *cfg, Term *t) {
    if (cfg->resizable)
        SetWindowMinSize(view_pixel_width(v, cfg, 2), view_pixel_height(v, cfg, 1));
    if (grid_follows_window(cfg)) return fit_grid(v, cfg, t);

    SetWindowSize(view_pixel_width(v, cfg, t->cols), view_pixel_height(v, cfg, t->rows));
    return false;
}

void window_toggle_fullscreen(void) {
    ToggleBorderlessWindowed();
}