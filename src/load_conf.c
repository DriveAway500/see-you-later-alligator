#include "load_conf.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { T_STR, T_INT, T_FLOAT, T_BOOL, T_COLOR };

typedef struct {
    const char *key;
    int         type;
    size_t      off, size;
    double      lo, hi;
} Opt;

#define O_STR(k, f)         { k, T_STR,   offsetof(Config, f), sizeof(((Config *)0)->f), 0, 0 }
#define O_INT(k, f, lo, hi) { k, T_INT,   offsetof(Config, f), 0, lo, hi }
#define O_FLT(k, f, lo, hi) { k, T_FLOAT, offsetof(Config, f), 0, lo, hi }
#define O_BOOL(k, f)        { k, T_BOOL,  offsetof(Config, f), 0, 0, 0 }
#define O_COL(k, f)         { k, T_COLOR, offsetof(Config, f), 0, 0, 0 }

static const Opt OPTS[] = {
    O_STR ("font_path",        font_path),
    O_INT ("font_size",        font_size,        6, 200),
    O_FLT ("line_height",      line_height,      0.5, 3.0),
    O_INT ("letter_spacing",   letter_spacing,   -10, 50),
    O_BOOL("font_smooth",      font_smooth),

    O_STR ("title",            title),
    O_INT ("cols",             cols,             2, 1000),
    O_INT ("rows",             rows,             1, 1000),
    O_INT ("padding",          padding,          0, 200),
    O_BOOL("decorated",        decorated),
    O_BOOL("resizable",        resizable),
    O_FLT ("opacity",          opacity,          0.0, 1.0),
    O_INT ("fps",              fps,              5, 360),

    O_STR ("shell",            shell),
    O_STR ("term",             term),

    O_INT ("scrollback",       scrollback,       0, 1000000),
    O_INT ("scroll_lines",     scroll_lines,     1, 100),

    O_STR ("cursor_style",     cursor_style),
    O_BOOL("cursor_blink",     cursor_blink),
    O_INT ("cursor_blink_ms",  cursor_blink_ms,  100, 5000),
    O_INT ("cursor_thickness", cursor_thickness, 1, 20),

    O_BOOL("bold_bright",      bold_bright),
    O_BOOL("fake_bold",        fake_bold),
    O_BOOL("copy_on_select",   copy_on_select),

    O_COL ("fg",               fg),
    O_COL ("bg",               bg),
    O_COL ("cursor_color",     cursor_color),
    O_COL ("selection_fg",     selection_fg),
    O_COL ("selection_bg",     selection_bg),
    O_COL ("color0",  palette[0]),  O_COL ("color1",  palette[1]),
    O_COL ("color2",  palette[2]),  O_COL ("color3",  palette[3]),
    O_COL ("color4",  palette[4]),  O_COL ("color5",  palette[5]),
    O_COL ("color6",  palette[6]),  O_COL ("color7",  palette[7]),
    O_COL ("color8",  palette[8]),  O_COL ("color9",  palette[9]),
    O_COL ("color10", palette[10]), O_COL ("color11", palette[11]),
    O_COL ("color12", palette[12]), O_COL ("color13", palette[13]),
    O_COL ("color14", palette[14]), O_COL ("color15", palette[15]),

    O_STR ("key_copy",         key_copy),
    O_STR ("key_paste",        key_paste),
    O_STR ("key_scroll_up",    key_scroll_up),
    O_STR ("key_scroll_down",  key_scroll_down),
};

static Rgb hex(unsigned v) {
    Rgb c = { (v >> 16) & 255, (v >> 8) & 255, v & 255 };
    return c;
}

void set_defaults(Config *c) {
    static const unsigned pal[16] = {
        0x1d1f21, 0xcc6666, 0xb5bd68, 0xf0c674, 0x81a2be, 0xb294bb, 0x8abeb7, 0xc5c8c6,
        0x969896, 0xcc6666, 0xb5bd68, 0xf0c674, 0x81a2be, 0xb294bb, 0x8abeb7, 0xffffff,
    };
    const char *sh = getenv("SHELL");

    memset(c, 0, sizeof *c);

    c->font_size      = 20;
    c->line_height    = 1.2f;
    c->font_smooth    = true;

    snprintf(c->title, sizeof c->title, "silly-term");
    c->cols           = 80;
    c->rows           = 24;
    c->padding        = 4;
    c->decorated      = true;
    c->resizable      = true;
    c->opacity        = 1.0f;
    c->fps            = 60;

    snprintf(c->shell, sizeof c->shell, "%s", (sh && *sh) ? sh : "/bin/bash");
    snprintf(c->term, sizeof c->term, "xterm-256color");

    c->scrollback     = 10000;
    c->scroll_lines   = 3;

    snprintf(c->cursor_style, sizeof c->cursor_style, "block");
    c->cursor_blink      = true;
    c->cursor_blink_ms   = 600;
    c->cursor_thickness  = 2;

    c->bold_bright    = true;

    c->fg = hex(0xc5c8c6);
    c->bg = hex(0x1d1f21);
    c->cursor_color = hex(0xc5c8c6);
    c->selection_fg = hex(0xffffff);
    c->selection_bg = hex(0x373b41);
    for (int i = 0; i < 16; i++) c->palette[i] = hex(pal[i]);

    snprintf(c->key_copy,        sizeof c->key_copy,        "ctrl+shift+c");
    snprintf(c->key_paste,       sizeof c->key_paste,       "ctrl+shift+v");
    snprintf(c->key_scroll_up,   sizeof c->key_scroll_up,   "shift+pageup");
    snprintf(c->key_scroll_down, sizeof c->key_scroll_down, "shift+pagedown");
}

int default_conf_path(char *out, size_t out_sz) {
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && *xdg)
        n = snprintf(out, out_sz, "%s/silly-term/sillyconf.conf", xdg);
    else if (home && *home)
        n = snprintf(out, out_sz, "%s/.config/silly-term/sillyconf.conf", home);
    else
        return -1;
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int parse_color(const char *s, Rgb *o) {
    char buf[7];
    if (*s == '#') s++;
    size_t n = strlen(s);
    if (n != 6 && n != 3) return -1;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[i])) return -1;
    if (n == 3) {
        buf[0] = buf[1] = s[0]; buf[2] = buf[3] = s[1]; buf[4] = buf[5] = s[2];
        buf[6] = 0;
        s = buf;
    }
    *o = hex((unsigned)strtoul(s, NULL, 16));
    return 0;
}

static int parse_bool(const char *s, bool *o) {
    if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") || !strcasecmp(s, "on")  || !strcmp(s, "1")) { *o = true;  return 0; }
    if (!strcasecmp(s, "false")|| !strcasecmp(s, "no")  || !strcasecmp(s, "off") || !strcmp(s, "0")) { *o = false; return 0; }
    return -1;
}

int load_conf(const char *path, Config *out) {
    set_defaults(out);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[1024];
    int  lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;

        char *eq = strchr(s, '=');
        if (!eq) { fprintf(stderr, "%s:%d: missing '='\n", path, lineno); continue; }
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);

        /* inline comment: whitespace followed by '#'  (so "#rrggbb" still works) */
        for (char *p = val; *p; p++)
            if (isspace((unsigned char)p[0]) && p[1] == '#') { *p = 0; break; }
        val = trim(val);

        size_t vl = strlen(val);
        if (vl >= 2 && (val[0] == '"' || val[0] == '\'') && val[vl - 1] == val[0]) {
            val[vl - 1] = 0;
            val++;
        }

        const Opt *o = NULL;
        for (size_t i = 0; i < sizeof OPTS / sizeof *OPTS; i++)
            if (!strcmp(OPTS[i].key, key)) { o = &OPTS[i]; break; }
        if (!o) { fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineno, key); continue; }

        char *dst = (char *)out + o->off;
        char *end;
        switch (o->type) {
        case T_STR:
            snprintf(dst, o->size, "%s", val);
            break;
        case T_INT: {
            long v = strtol(val, &end, 10);
            if (!*val || *end) { fprintf(stderr, "%s:%d: '%s' is not an integer\n", path, lineno, key); break; }
            if (v < o->lo) v = (long)o->lo;
            if (v > o->hi) v = (long)o->hi;
            *(int *)dst = (int)v;
            break;
        }
        case T_FLOAT: {
            double v = strtod(val, &end);
            if (!*val || *end) { fprintf(stderr, "%s:%d: '%s' is not a number\n", path, lineno, key); break; }
            if (v < o->lo) v = o->lo;
            if (v > o->hi) v = o->hi;
            *(float *)dst = (float)v;
            break;
        }
        case T_BOOL:
            if (parse_bool(val, (bool *)dst) != 0)
                fprintf(stderr, "%s:%d: '%s' expects true/false\n", path, lineno, key);
            break;
        case T_COLOR:
            if (parse_color(val, (Rgb *)dst) != 0)
                fprintf(stderr, "%s:%d: '%s' expects #rrggbb\n", path, lineno, key);
            break;
        }
    }
    fclose(f);
    return 0;
}