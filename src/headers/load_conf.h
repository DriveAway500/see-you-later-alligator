#ifndef LOAD_CONF_H
#define LOAD_CONF_H

#include <stdbool.h>
#include <stddef.h>

typedef struct { unsigned char r, g, b; } Rgb;

typedef struct {
    /* font */
    char  font_path[256];     /* empty = try common system fonts */
    int   font_size;
    float line_height;        /* multiplier of font_size */
    int   letter_spacing;     /* extra px added to cell width */
    bool  font_smooth;

    /* window */
    char  title[128];
    int   cols, rows;
    int   padding;
    bool  decorated;
    bool  resizable;
    float opacity;            /* 0.0 - 1.0 (background only) */
    int   fps;

    /* shell */
    char  shell[256];         /* command line, split on spaces */
    char  term[64];           /* $TERM for the child */

    /* scrolling */
    int   scrollback;         /* lines kept */
    int   scroll_lines;       /* lines per wheel notch */

    /* cursor */
    char  cursor_style[16];   /* block | underline | bar */
    bool  cursor_blink;
    int   cursor_blink_ms;
    int   cursor_thickness;

    /* text rendering */
    bool  bold_bright;
    bool  fake_bold;

    /* colors */
    Rgb   fg, bg, cursor_color, selection_fg, selection_bg;
    Rgb   palette[16];

    /* selection */
    bool  copy_on_select;

    /* keybinds: "ctrl+shift+c", "shift+pageup", "f11", ... */
    char  key_copy[48];
    char  key_paste[48];
    char  key_scroll_up[48];
    char  key_scroll_down[48];
} Config;

void set_defaults(Config *c);
int  default_conf_path(char *out, size_t out_sz);
/* Always leaves *out fully initialised (defaults + whatever the file set).
   Returns 0 on success, -1 if the file could not be opened. */
int  load_conf(const char *path, Config *out);

#endif