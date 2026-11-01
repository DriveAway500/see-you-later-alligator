#ifndef SELECTION_H
#define SELECTION_H

#include <stdbool.h>
#include "term.h"

/* Linear (reading-order) selection in viewport coordinates. */
typedef struct {
    bool active, dragging;
    int  r0, c0, r1, c1;
} Selection;

/* Selection as a linear range [a, b] of row * cols + col (a <= b).
   Returns false when nothing is selected. */
bool  sel_range(const Selection *s, const Term *t, long *a, long *b);

bool  sel_contains(const Selection *s, const Term *t, int row, int col);

/* Selected text as malloc'd UTF-8 (caller frees), or NULL if nothing selected. */
char *sel_text(const Selection *s, const Term *t);

#endif