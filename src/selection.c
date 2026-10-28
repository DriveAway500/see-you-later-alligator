#include "headers/selection.h"

#include <stdint.h>
#include <stdlib.h>

#include "headers/util.h"

static void ordered(const Selection *s, const Term *t, long *a, long *b) {
    *a = (long)s->r0 * t->cols + s->c0;
    *b = (long)s->r1 * t->cols + s->c1;
    if (*a > *b) { long x = *a; *a = *b; *b = x; }
}

bool sel_contains(const Selection *s, const Term *t, int row, int col) {
    if (!s->active) return false;
    long a, b;
    ordered(s, t, &a, &b);
    long p = (long)row * t->cols + col;
    return p >= a && p <= b;
}

char *sel_text(const Selection *s, const Term *t) {
    if (!s->active) return NULL;

    long a, b;
    ordered(s, t, &a, &b);
    int ra = (int)(a / t->cols), ca = (int)(a % t->cols);
    int rb = (int)(b / t->cols), cb = (int)(b % t->cols);

    size_t cap = (size_t)(b - a + 1) * 4 * VTERM_MAX_CHARS_PER_CELL + (size_t)t->rows + 2;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n = 0;

    for (int r = ra; r <= rb; r++) {
        int c0 = (r == ra) ? ca : 0;
        int c1 = (r == rb) ? cb : t->cols - 1;
        size_t start = n;

        for (int c = c0; c <= c1; c++) {
            VTermScreenCell cell;
            if (!term_get_cell(t, r, c, &cell) || cell.chars[0] == 0) { buf[n++] = ' '; continue; }
            if (cell.chars[0] == (uint32_t)-1) continue;          /* right half of wide char */
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++)
                n += (size_t)utf8_encode(cell.chars[i], buf + n);
        }

        while (n > start && buf[n - 1] == ' ') n--;               /* trim trailing blanks */
        if (r < rb) buf[n++] = '\n';
    }
    buf[n] = 0;
    return buf;
}