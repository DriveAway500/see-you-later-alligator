#ifndef TERM_H
#define TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <vterm.h>

#include "load_conf.h"

/* One scrollback line. cap is the allocated capacity in cells (>= cols) and
   survives pop/overwrite so the buffer can be reused. */
typedef struct {
    int              cols;   /* valid cells in this line */
    int              cap;    /* allocated cells */
    VTermScreenCell *cells;
} SbLine;

/* Terminal core: libvterm + pty + scrollback. No GUI dependencies. */
typedef struct Term {
    VTerm       *vt;
    VTermScreen *screen;
    VTermState  *state;

    int          master;            /* pty master fd, -1 if none (O_NONBLOCK after term_init) */
    pid_t        child;
    int          rows, cols;

    SbLine      *sb;                /* ring buffer of scrollback lines */
    int          sb_cap, sb_head, sb_count;
    int          sb_off;            /* lines the view is scrolled back; invariant 0 <= sb_off <= sb_count */

    bool         cursor_visible;
    bool         altscreen;
    int          mouse_mode;        /* VTERM_PROP_MOUSE_NONE/CLICK/DRAG/MOVE, set by the app */

    char         title[256];
    size_t       title_len;
    void       (*on_title)(const char *title);   /* optional */

    char         pump_buf[16384];   /* read buffer for term_pump (per Term, reentrant) */
} Term;

/* 0 on success, -1 on failure. On failure everything is already released and
   *t is zeroed: the caller must NOT call term_free. */
int   term_init(Term *t, const Config *cfg);
void  term_free(Term *t);

/* Read pending pty output into libvterm (at most 512 KiB per call).
   >0: data was processed (also when the per-call bound was hit, so call again),
    0: idle, -1: shell exited or pty I/O error. */
int   term_pump(Term *t);

void  term_write(Term *t, const char *buf, size_t n);   /* send bytes to the shell */
void  term_resize(Term *t, int cols, int rows);
void  term_scroll(Term *t, int lines);                  /* +up (back), -down */
void  term_clear_scrollback(Term *t);                   /* also releases the line buffers */

/* vrow is a viewport row (0..rows-1); honours sb_off. */
bool  term_get_cell(const Term *t, int vrow, int col, VTermScreenCell *cell);

/*
 * Copy up to max cells of viewport row vrow into out (honours sb_off).
 * Returns how many cells were filled, 0 if vrow is out of range. Cells are
 * never written past min(max, t->cols). A scrollback row shorter than that
 * (line saved at a smaller width) fills only its own length; the caller
 * treats the rest as blank, like term_get_cell returning false.
 * Scrollback rows are one memcpy; viewport rows are one libvterm call per cell.
 */
int   term_get_row(const Term *t, int vrow, VTermScreenCell *out, int max);

#endif