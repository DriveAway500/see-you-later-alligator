#include "headers/term.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "headers/pty_io.h"

#define PUMP_BOUND  ((size_t)1 << 19)   /* max bytes consumed per term_pump call */
#define PUMP_EINTR  16                  /* max consecutive EINTR retries */

/* ------------------------------------------------------------- scrollback */

static SbLine *sb_line(const Term *t, int i) {
    return &t->sb[(t->sb_head + i) % t->sb_cap];
}

/* FIX: single place that keeps 0 <= sb_off <= sb_count */
static inline void sb_clamp_off(Term *t) {
    if (t->sb_off < 0)           t->sb_off = 0;
    if (t->sb_off > t->sb_count) t->sb_off = t->sb_count;
}

static inline void sb_check(const Term *t) {
    assert(t->sb_off >= 0 && t->sb_off <= t->sb_count);
    assert(t->sb_count >= 0 && t->sb_count <= t->sb_cap);
}

/* free every line buffer, including slots outside the live window
   (popped/overwritten lines keep their buffer for reuse) */
static void sb_release(Term *t) {
    if (t->sb)
        for (int i = 0; i < t->sb_cap; i++) {
            free(t->sb[i].cells);
            t->sb[i].cells = NULL;
            t->sb[i].cols = t->sb[i].cap = 0;
        }
}

/* FIX: blank cell with default colors (a zeroed VTermColor is RGB black,
   which would draw a black rectangle in the renderer) */
static void blank_cell(VTermScreenCell *c) {
    memset(c, 0, sizeof *c);
    c->chars[0] = ' ';
    c->width    = 1;
    c->fg.type  = VTERM_COLOR_DEFAULT_FG;
    c->bg.type  = VTERM_COLOR_DEFAULT_BG;
}

static int cb_pushline(int cols, const VTermScreenCell *cells, void *u) {
    Term *t = u;
    if (t->sb_cap <= 0 || cols <= 0) return 0;

    SbLine *l;
    if (t->sb_count == t->sb_cap) {
        /* OPT: ring full, recycle the oldest slot and its buffer (no free/malloc) */
        l = &t->sb[t->sb_head];
        t->sb_head = (t->sb_head + 1) % t->sb_cap;
    } else {
        l = &t->sb[(t->sb_head + t->sb_count++) % t->sb_cap];
    }

    if (l->cap < cols) {
        /* OPT: allocate lazily and only grow; no copy of the stale contents */
        free(l->cells);
        l->cells = malloc((size_t)cols * sizeof *cells);
        l->cap = l->cells ? cols : 0;
    }
    if (!l->cells) { l->cols = 0; return 1; }
    memcpy(l->cells, cells, (size_t)cols * sizeof *cells);
    l->cols = cols;

    /* FIX: keep the view stable whenever it is scrolled back (was: only if
       sb_off < sb_count); the ring does not grow once full, so clamp */
    if (t->sb_off > 0) {
        t->sb_off++;
        sb_clamp_off(t);
    }
    return 1;
}

/* FIX: was a stub returning 0; now restores lines into the main screen
   when leaving altscreen or growing the window */
static int cb_popline(int cols, VTermScreenCell *cells, void *u) {
    Term *t = u;
    if (t->sb_count <= 0 || cols <= 0) return 0;

    const SbLine *l = sb_line(t, t->sb_count - 1);
    int n = l->cols < cols ? l->cols : cols;
    if (n > 0 && l->cells)
        memcpy(cells, l->cells, (size_t)n * sizeof *cells);

    if (n < cols) {
        blank_cell(&cells[n]);
        for (int i = n + 1; i < cols; i++)
            cells[i] = cells[n];
    }

    t->sb_count--;              /* slot keeps its buffer; head is unchanged */
    sb_clamp_off(t);
    return 1;
}

/* -------------------------------------------------------- vterm callbacks */

static int cb_prop(VTermProp prop, VTermValue *val, void *u) {
    Term *t = u;
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        t->cursor_visible = val->boolean;
        break;
    case VTERM_PROP_MOUSE:
        t->mouse_mode = val->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        t->altscreen = val->boolean;
        t->sb_off = 0;
        break;
    case VTERM_PROP_TITLE: {
        VTermStringFragment fr = val->string;
        if (fr.initial) t->title_len = 0;
        /* FIX: defensive clamp of title_len before computing the room left */
        if (t->title_len > sizeof t->title - 1) t->title_len = sizeof t->title - 1;
        /* FIX: ignore a fragment with len > 0 but no data */
        if (fr.len > 0 && fr.str) {
            size_t room = sizeof t->title - 1 - t->title_len;
            size_t n = fr.len < room ? fr.len : room;
            memcpy(t->title + t->title_len, fr.str, n);
            t->title_len += n;
        }
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

/* FIX: ESC[3J (used by `clear`) must drop the scrollback */
static int cb_sbclear(void *u) {
    Term *t = u;
    term_clear_scrollback(t);   /* frees lines and zeroes head/count/off */
    return 1;
}

static const VTermScreenCallbacks SCREEN_CB = {
    .settermprop = cb_prop,
    .sb_pushline = cb_pushline,
    .sb_popline  = cb_popline,
    .sb_clear    = cb_sbclear,
};

/* ------------------------------------------------------------- public API */

/* FIX: SIGHUP, wait up to ~100ms, then SIGKILL; never leaves a zombie */
static void reap_child(pid_t pid) {
    kill(pid, SIGHUP);
    for (int i = 0; i < 100; i++) {
        pid_t r = waitpid(pid, NULL, WNOHANG);
        if (r == pid || (r < 0 && errno != EINTR)) return;
        usleep(1000);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
}

/* releases whatever is set, safe on a partially initialised Term */
static void term_teardown(Term *t) {
    if (t->master >= 0) close(t->master);
    if (t->child > 0) reap_child(t->child);
    sb_release(t);
    free(t->sb);
    if (t->vt) vterm_free(t->vt);
    memset(t, 0, sizeof *t);
    t->master = -1;
}

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
    if (!t->vt) goto fail;                       /* FIX: sb was leaked here */
    vterm_set_utf8(t->vt, 1);
    t->screen = vterm_obtain_screen(t->vt);
    t->state  = vterm_obtain_state(t->vt);
    vterm_output_set_callback(t->vt, cb_output, t);
    vterm_screen_set_callbacks(t->screen, &SCREEN_CB, t);
    vterm_screen_enable_altscreen(t->screen, 1);
    vterm_screen_reset(t->screen, 1);

    t->child = pty_spawn(&t->master, cfg->shell, cfg->term, t->rows, t->cols);
    if (t->child < 0) { t->child = 0; goto fail; }   /* FIX: vt and sb were leaked */

    /* FIX: term_pump assumes a non-blocking master. Set it here if pty_spawn
       did not (pressure: pty_write_all must tolerate EAGAIN on this fd). */
    int fl = fcntl(t->master, F_GETFL, 0);
    if (fl < 0) goto fail;
    if (!(fl & O_NONBLOCK) && fcntl(t->master, F_SETFL, fl | O_NONBLOCK) < 0) goto fail;
    return 0;

fail:
    /* the caller must NOT call term_free after a failed term_init */
    term_teardown(t);
    return -1;
}

void term_free(Term *t) {
    term_teardown(t);
}

int term_pump(Term *t) {
    size_t total = 0;
    int got = 0;
    int eintr = 0;

    /* OPT: per-Term buffer instead of a 64K static (reentrant, smaller) */
    while (total < PUMP_BOUND) {                 /* bound work per call */
        ssize_t n = read(t->master, t->pump_buf, sizeof t->pump_buf);
        if (n > 0) {
            vterm_input_write(t->vt, t->pump_buf, (size_t)n);
            total += (size_t)n;
            got = 1;
            eintr = 0;
        } else if (n == 0) {
            return -1;                           /* EOF: shell exited */
        } else if (errno == EINTR) {
            if (++eintr >= PUMP_EINTR) break;    /* FIX: bounded retries */
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;                               /* drained */
        } else {
            return -1;                           /* EIO etc: slave closed */
        }
    }
    /* OPT: hitting the bound still returns 1, so the event loop calls again
       and nothing is lost (the rest stays in the kernel buffer) */
    sb_check(t);
    return got;
}

void term_write(Term *t, const char *buf, size_t n) {
    if (t->master >= 0) pty_write_all(t->master, buf, n);
}

void term_resize(Term *t, int cols, int rows) {
    if (cols < 1 || rows < 1) return;
    vterm_set_size(t->vt, rows, cols);           /* may call push/popline */
    if (t->master >= 0) pty_set_size(t->master, rows, cols);
    t->rows = rows;
    t->cols = cols;
    sb_clamp_off(t);                             /* FIX: revalidated after the resize */
    sb_check(t);
}

void term_scroll(Term *t, int lines) {
    if (t->altscreen) return;
    t->sb_off += lines;
    sb_clamp_off(t);                             /* same helper as every other path */
    sb_check(t);
}

void term_clear_scrollback(Term *t) {
    sb_release(t);                               /* frees buffers, incl. recycled slots */
    t->sb_head = t->sb_count = t->sb_off = 0;
}

bool term_get_cell(const Term *t, int vrow, int col, VTermScreenCell *cell) {
    if (vrow < 0 || vrow >= t->rows) return false;   /* FIX: bounds check */
    int v = vrow - t->sb_off;
    if (v >= 0) {
        VTermPos p = { .row = v, .col = col };
        return vterm_screen_get_cell(t->screen, p, cell) != 0;
    }
    int idx = t->sb_count + v;
    if (idx < 0 || col < 0) return false;
    const SbLine *l = sb_line(t, idx);
    if (col >= l->cols) return false;
    *cell = l->cells[col];
    return true;
}

/* OPT: whole row per call; one memcpy for scrollback */
int term_get_row(const Term *t, int vrow, VTermScreenCell *out, int max) {
    if (vrow < 0 || vrow >= t->rows || max <= 0) return 0;
    int lim = max < t->cols ? max : t->cols;

    int v = vrow - t->sb_off;
    if (v >= 0) {
        int n = 0;
        for (; n < lim; n++) {
            VTermPos p = { .row = v, .col = n };
            if (!vterm_screen_get_cell(t->screen, p, &out[n])) break;
        }
        return n;
    }

    int idx = t->sb_count + v;
    if (idx < 0) return 0;
    const SbLine *l = sb_line(t, idx);
    int n = l->cols < lim ? l->cols : lim;
    if (n > 0) memcpy(out, l->cells, (size_t)n * sizeof *out);
    return n;
}