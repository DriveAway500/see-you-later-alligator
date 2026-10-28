#include "headers/term.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "headers/pty_io.h"

/* ------------------------------------------------------------- scrollback */

static SbLine *sb_line(const Term *t, int i) {
    return &t->sb[(t->sb_head + i) % t->sb_cap];
}

static int cb_pushline(int cols, const VTermScreenCell *cells, void *u) {
    Term *t = u;
    if (t->sb_cap <= 0) return 0;

    SbLine *l;
    if (t->sb_count == t->sb_cap) {
        l = &t->sb[t->sb_head];
        free(l->cells);
        t->sb_head = (t->sb_head + 1) % t->sb_cap;
    } else {
        l = &t->sb[(t->sb_head + t->sb_count++) % t->sb_cap];
    }

    l->cells = malloc((size_t)cols * sizeof *cells);
    if (!l->cells) { l->cols = 0; return 1; }
    memcpy(l->cells, cells, (size_t)cols * sizeof *cells);
    l->cols = cols;

    if (t->sb_off > 0 && t->sb_off < t->sb_count) t->sb_off++;   /* keep view stable */
    return 1;
}

static int cb_popline(int cols, VTermScreenCell *cells, void *u) {
    (void)cols; (void)cells; (void)u;
    return 0;
}

/* -------------------------------------------------------- vterm callbacks */

static int cb_prop(VTermProp prop, VTermValue *val, void *u) {
    Term *t = u;
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        t->cursor_visible = val->boolean;
        break;
    case VTERM_PROP_ALTSCREEN:
        t->altscreen = val->boolean;
        t->sb_off = 0;
        break;
    case VTERM_PROP_TITLE: {
        VTermStringFragment fr = val->string;
        if (fr.initial) t->title_len = 0;
        size_t n = fr.len;
        if (t->title_len + n >= sizeof t->title) n = sizeof t->title - 1 - t->title_len;
        memcpy(t->title + t->title_len, fr.str, n);
        t->title_len += n;
        if (fr.final) {
            t->title[t->title_len] = 0;
            if (t->on_title) t->on_title(t->title);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

static void cb_output(const char *s, size_t len, void *u) {
    Term *t = u;
    if (t->master >= 0) pty_write_all(t->master, s, len);
}

static const VTermScreenCallbacks SCREEN_CB = {
    .settermprop = cb_prop,
    .sb_pushline = cb_pushline,
    .sb_popline  = cb_popline,
};

/* ------------------------------------------------------------- public API */

int term_init(Term *t, const Config *cfg) {
    memset(t, 0, sizeof *t);
    t->master         = -1;
    t->rows           = cfg->rows;
    t->cols           = cfg->cols;
    t->cursor_visible = true;

    if (cfg->scrollback > 0) {
        t->sb = calloc((size_t)cfg->scrollback, sizeof *t->sb);
        t->sb_cap = t->sb ? cfg->scrollback : 0;
    }

    t->vt = vterm_new(t->rows, t->cols);
    if (!t->vt) return -1;
    vterm_set_utf8(t->vt, 1);
    t->screen = vterm_obtain_screen(t->vt);
    t->state  = vterm_obtain_state(t->vt);
    vterm_output_set_callback(t->vt, cb_output, t);
    vterm_screen_set_callbacks(t->screen, &SCREEN_CB, t);
    vterm_screen_enable_altscreen(t->screen, 1);
    vterm_screen_reset(t->screen, 1);

    t->child = pty_spawn(&t->master, cfg->shell, cfg->term, t->rows, t->cols);
    if (t->child < 0) return -1;
    return 0;
}

void term_free(Term *t) {
    if (t->master >= 0) close(t->master);
    if (t->child > 0) {
        kill(t->child, SIGHUP);
        waitpid(t->child, NULL, WNOHANG);
    }
    for (int i = 0; i < t->sb_count; i++) free(sb_line(t, i)->cells);
    free(t->sb);
    if (t->vt) vterm_free(t->vt);
    memset(t, 0, sizeof *t);
    t->master = -1;
}

int term_pump(Term *t) {
    static char buf[65536];
    size_t total = 0;
    int got = 0;
    while (total < (1u << 19)) {                 /* bound work per frame */
        ssize_t n = read(t->master, buf, sizeof buf);
        if (n > 0) {
            vterm_input_write(t->vt, buf, (size_t)n);
            total += (size_t)n;
            got = 1;
        } else if (n == 0) {
            return -1;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN) {
            break;
        } else {
            return -1;                           /* EIO: shell closed */
        }
    }
    return got;
}

void term_write(Term *t, const char *buf, size_t n) {
    if (t->master >= 0) pty_write_all(t->master, buf, n);
}

void term_resize(Term *t, int cols, int rows) {
    vterm_set_size(t->vt, rows, cols);
    pty_set_size(t->master, rows, cols);
    t->rows = rows;
    t->cols = cols;
    if (t->sb_off > t->sb_count) t->sb_off = t->sb_count;
}

void term_scroll(Term *t, int lines) {
    if (t->altscreen) return;
    t->sb_off += lines;
    if (t->sb_off < 0) t->sb_off = 0;
    if (t->sb_off > t->sb_count) t->sb_off = t->sb_count;
}

bool term_get_cell(const Term *t, int vrow, int col, VTermScreenCell *cell) {
    int v = vrow - t->sb_off;
    if (v >= 0) {
        VTermPos p = { .row = v, .col = col };
        return vterm_screen_get_cell(t->screen, p, cell) != 0;
    }
    int idx = t->sb_count + v;
    if (idx < 0) return false;
    const SbLine *l = sb_line(t, idx);
    if (col >= l->cols) return false;
    *cell = l->cells[col];
    return true;
}