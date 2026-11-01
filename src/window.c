#include "headers/window.h"

#include <stdio.h>

static bool is_wayland(void) {
#ifdef GLFW_PLATFORM_WAYLAND
    return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
    return false;
#endif
}

static bool is_fullscreen(const App *a) { return glfwGetWindowMonitor(a->win) != NULL; }

/* Windows we may resize the grid for: resizable ones, or any window while
   it is fullscreen. */
static bool grid_follows_window(const App *a) { return a->cfg->resizable || is_fullscreen(a); }

static void read_sizes(App *a) {
    int ww, wh;
    glfwGetWindowSize(a->win, &ww, &wh);
    glfwGetFramebufferSize(a->win, &a->fbw, &a->fbh);
    if (ww > 0 && wh > 0) {
        a->sx = (float)a->fbw / (float)ww;
        a->sy = (float)a->fbh / (float)wh;
    }
    if (a->sx < 1.0f) a->sx = 1.0f;
    if (a->sy < 1.0f) a->sy = 1.0f;
}

static bool fit_grid(App *a) {
    const View *v = &a->view;
    const Config *cfg = a->cfg;
    Term *t = &a->term;
    int nc = (a->fbw - 2 * cfg->padding) / v->cw;
    int nr = (a->fbh - 2 * cfg->padding) / v->ch;
    if (nc < 1) nc = 1;
    if (nr < 1) nr = 1;
    if (nc == t->cols && nr == t->rows) return false;
    term_resize(t, nc, nr);
    return true;
}

/* ---------------------------------------------------------------- callbacks */

static void on_error(int code, const char *desc) {
#ifdef GLFW_FEATURE_UNAVAILABLE
    if (code == GLFW_FEATURE_UNAVAILABLE) return;        /* e.g. window position on Wayland */
#endif
    fprintf(stderr, "silly-term: GLFW error %d: %s\n", code, desc);
}

static void on_fb_size(GLFWwindow *w, int width, int height) {
    App *a = glfwGetWindowUserPointer(w);
    a->fbw = width;
    a->fbh = height;
    a->resized = true;
    a->redraw = true;
}

static void on_refresh(GLFWwindow *w) {                   /* expose / compositor asked for a repaint */
    App *a = glfwGetWindowUserPointer(w);
    a->redraw = true;
}

static void on_focus(GLFWwindow *w, int focused) {
    App *a = glfwGetWindowUserPointer(w);
    a->focused = focused != 0;
    if (focused) a->input.last_input = glfwGetTime();    /* restart the blink phase */
    a->redraw = true;
}

static void on_iconify(GLFWwindow *w, int iconified) {
    App *a = glfwGetWindowUserPointer(w);
    a->iconified = iconified != 0;
    if (!iconified) a->redraw = true;
}

/* ---------------------------------------------------------------------- API */

int window_open(App *a) {
    const Config *cfg = a->cfg;

    glfwSetErrorCallback(on_error);
    if (!glfwInit()) {
        fprintf(stderr, "silly-term: cannot initialise GLFW\n");
        return -1;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);              /* hidden until we know the real size */
    glfwWindowHint(GLFW_DECORATED, cfg->decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, cfg->resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, cfg->opacity < 1.0f ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
#ifdef GLFW_X11_CLASS_NAME
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "silly-term");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "silly-term");
#endif
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "silly-term");
#endif

    a->win = glfwCreateWindow(100, 100, cfg->title, NULL, NULL);
    if (!a->win) {
        fprintf(stderr, "silly-term: cannot create an OpenGL 3.3 window\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(a->win);
    if (gl_load() != 0) { window_close(a); return -1; }

    /* Vsync on X11/other. On Wayland a swap with interval 1 can block for as long
       as the window is not visible, which would stall the pty; the fps cap in
       main paces the frames there instead. */
    glfwSwapInterval(is_wayland() ? 0 : 1);

    a->focused = true;
    read_sizes(a);

    glfwSetWindowUserPointer(a->win, a);
    glfwSetFramebufferSizeCallback(a->win, on_fb_size);
    glfwSetWindowRefreshCallback(a->win, on_refresh);
    glfwSetWindowFocusCallback(a->win, on_focus);
    glfwSetWindowIconifyCallback(a->win, on_iconify);
    return 0;
}

void window_show(App *a, int cols, int rows) {
    const View *v = &a->view;
    const Config *cfg = a->cfg;

    glfwSetWindowSize(a->win, (int)((float)view_pixel_width(v, cfg, cols)  / a->sx + 0.5f),
                              (int)((float)view_pixel_height(v, cfg, rows) / a->sy + 0.5f));
    if (cfg->resizable)
        glfwSetWindowSizeLimits(a->win,
                                (int)((float)view_pixel_width(v, cfg, 2)  / a->sx + 0.5f),
                                (int)((float)view_pixel_height(v, cfg, 1) / a->sy + 0.5f),
                                GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwShowWindow(a->win);
    read_sizes(a);
    a->redraw = true;
}

bool window_handle_resize(App *a) {
    if (!a->resized) return false;
    a->resized = false;
    glfwGetFramebufferSize(a->win, &a->fbw, &a->fbh);
    if (!grid_follows_window(a)) return false;
    return fit_grid(a);
}

bool window_refit(App *a) {
    const View *v = &a->view;
    const Config *cfg = a->cfg;

    if (cfg->resizable)
        glfwSetWindowSizeLimits(a->win,
                                (int)((float)view_pixel_width(v, cfg, 2)  / a->sx + 0.5f),
                                (int)((float)view_pixel_height(v, cfg, 1) / a->sy + 0.5f),
                                GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (grid_follows_window(a)) {
        glfwGetFramebufferSize(a->win, &a->fbw, &a->fbh);
        return fit_grid(a);
    }
    glfwSetWindowSize(a->win, (int)((float)view_pixel_width(v, cfg, a->term.cols)  / a->sx + 0.5f),
                              (int)((float)view_pixel_height(v, cfg, a->term.rows) / a->sy + 0.5f));
    return false;
}

static GLFWmonitor *current_monitor(GLFWwindow *w) {
    GLFWmonitor *best = glfwGetPrimaryMonitor();
    if (is_wayland()) return best;                          /* no window position on Wayland */

    int wx, wy, ww, wh, n, best_overlap = 0;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    GLFWmonitor **mons = glfwGetMonitors(&n);
    for (int i = 0; i < n; i++) {
        const GLFWvidmode *m = glfwGetVideoMode(mons[i]);
        int mx, my;
        glfwGetMonitorPos(mons[i], &mx, &my);
        int ox = (wx + ww < mx + m->width  ? wx + ww : mx + m->width)  - (wx > mx ? wx : mx);
        int oy = (wy + wh < my + m->height ? wy + wh : my + m->height) - (wy > my ? wy : my);
        if (ox > 0 && oy > 0 && ox * oy > best_overlap) { best_overlap = ox * oy; best = mons[i]; }
    }
    return best;
}

void window_toggle_fullscreen(App *a) {
    if (is_fullscreen(a)) {
        glfwSetWindowMonitor(a->win, NULL, a->win_x, a->win_y, a->win_w, a->win_h, 0);
    } else {
        if (!is_wayland()) glfwGetWindowPos(a->win, &a->win_x, &a->win_y);
        glfwGetWindowSize(a->win, &a->win_w, &a->win_h);
        GLFWmonitor *m = current_monitor(a->win);
        if (!m) return;
        const GLFWvidmode *mode = glfwGetVideoMode(m);
        glfwSetWindowMonitor(a->win, m, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    a->resized = true;                                      /* refit once the size settles */
    a->redraw = true;
}

void window_close(App *a) {
    if (a->win) glfwDestroyWindow(a->win);
    a->win = NULL;
    glfwTerminate();
}
