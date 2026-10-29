#ifndef TERM_H
#define TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <vterm.h>

#include "load_conf.h"

typedef struct { int cols; VTermScreenCell *cells; } SbLine;

/* Terminal core: libvterm + pty + scrollback. No GUI dependencies. */
typedef struct Term {
    VTerm       *vt;
    VTermScreen *screen;
    VTermState  *state;

    int          master;            /* pty master fd, -1 if none */
    pid_t        child;
    int          rows, cols;

    SbLine      *sb;                /* ring buffer of scrollback lines */
    int          sb_cap, sb_head, sb_count;
    int          sb_off;            /* how many lines the view is scrolled back */

    bool         cursor_visible;
    bool         altscreen;
    int          mouse_mode;        /* VTERM_PROP_MOUSE_NONE/CLICK/DRAG/MOVE, set by the app */

    char         title[256];
    size_t       title_len;
    void       (*on_title)(const char *title);   /* optional */
} Term;

int   term_init(Term *t, const Config *cfg);        /* 0 on success */
void  term_free(Term *t);

/* Read pending pty output into libvterm.
   >0: data was processed, 0: idle, -1: shell exited. */
int   term_pump(Term *t);

void  term_write(Term *t, const char *buf, size_t n);   /* send bytes to the shell */
void  term_resize(Term *t, int cols, int rows);
void  term_scroll(Term *t, int lines);                  /* +up (back), -down */
void  term_clear_scrollback(Term *t);

/* vrow is a viewport row (0..rows-1); honours sb_off. */
bool  term_get_cell(const Term *t, int vrow, int col, VTermScreenCell *cell);

#endif