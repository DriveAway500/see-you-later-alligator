#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "raylib.h"
#include <vterm.h>
#include "load_conf.h"

/* ------------------------------------------------------------------ state */

typedef struct { int cols; VTermScreenCell *cells; } SbLine;

static Config cfg;

static struct {
    VTerm       *vt;
    VTermScreen *screen;
    VTermState  *state;
    int          master;
    pid_t        child;
    int          rows, cols;
    SbLine      *sb;                    /* ring buffer of scrollback lines */
    int          sb_cap, sb_head, sb_count, sb_off;
    bool         cursor_visible, altscreen;
    char         title[256];
    size_t       title_len;
} T = { .master = -1, .cursor_visible = true };

static struct { bool active, dragging; int r0, c0, r1, c1; } S;

static Font   g_font;
static int    g_cw, g_ch, g_fs;         /* cell width/height, font size */
static int    g_cursor_style;           /* 0 block, 1 underline, 2 bar */
static double g_last_input;

typedef struct { int key; bool ctrl, shift, alt; } Bind;
static Bind B_copy, B_paste, B_sup, B_sdown;

/* ------------------------------------------------------------------- util */

static Color C(Rgb c) { return (Color){ c.r, c.g, c.b, 255 }; }

static int utf8_enc(uint32_t cp, char *o) {
    if (cp < 0x80)    { o[0] = (char)cp; return 1; }
    if (cp < 0x800)   { o[0] = 0xC0 | (cp >> 6);  o[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 63); o[2] = 0x80 | (cp & 63); return 3; }
    o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 63);
    o[2] = 0x80 | ((cp >> 6) & 63); o[3] = 0x80 | (cp & 63);
    return 4;
}

static void write_all(int fd, const char *b, size_t n) {
    int tries = 0;
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN && ++tries < 100) {
                struct pollfd p = { fd, POLLOUT, 0 };
                poll(&p, 1, 50);
                continue;
            }
            return;
        }
        b += w; n -= (size_t)w;
    }
}

/* ------------------------------------------------------------- keybinds */

static int key_by_name(const char *t) {
    size_t n = strlen(t);
    if (n == 1) {
        if (isalpha((unsigned char)t[0])) return KEY_A + (tolower((unsigned char)t[0]) - 'a');
        if (isdigit((unsigned char)t[0])) return KEY_ZERO + (t[0] - '0');
    }
    if (t[0] == 'f' && n >= 2 && n <= 3 && isdigit((unsigned char)t[1])) {
        int f = atoi(t + 1);
        if (f >= 1 && f <= 12) return KEY_F1 + f - 1;
    }
    static const struct { const char *n; int k; } nm[] = {
        {"pageup", KEY_PAGE_UP}, {"pagedown", KEY_PAGE_DOWN}, {"home", KEY_HOME},
        {"end", KEY_END}, {"insert", KEY_INSERT}, {"delete", KEY_DELETE},
        {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
        {"space", KEY_SPACE}, {"enter", KEY_ENTER}, {"tab", KEY_TAB},
    };
    for (size_t i = 0; i < sizeof nm / sizeof *nm; i++)
        if (!strcmp(nm[i].n, t)) return nm[i].k;
    return 0;
}

static Bind parse_bind(const char *s) {
    Bind b = { 0 };
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (char *t = strtok(buf, "+ "); t; t = strtok(NULL, "+ ")) {
        if      (!strcmp(t, "ctrl") || !strcmp(t, "control")) b.ctrl = true;
        else if (!strcmp(t, "shift")) b.shift = true;
        else if (!strcmp(t, "alt"))   b.alt = true;
        else b.key = key_by_name(t);
    }
    return b;
}

static bool pressed(int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }

static bool bind_hit(const Bind *b, bool ctrl, bool shift, bool alt) {
    return b->key && b->ctrl == ctrl && b->shift == shift && b->alt == alt && pressed(b->key);
}

/* ------------------------------------------------------ vterm callbacks */

static void out_cb(const char *s, size_t len, void *u) {
    (void)u;
    if (T.master >= 0) write_all(T.master, s, len);
}

static SbLine *sb_line(int i) { return &T.sb[(T.sb_head + i) % T.sb_cap]; }

static int cb_pushline(int cols, const VTermScreenCell *cells, void *u) {
    (void)u;
    if (T.sb_cap <= 0) return 0;
    SbLine *l;
    if (T.sb_count == T.sb_cap) {
        l = &T.sb[T.sb_head];
        free(l->cells);
        T.sb_head = (T.sb_head + 1) % T.sb_cap;
    } else {
        l = &T.sb[(T.sb_head + T.sb_count++) % T.sb_cap];
    }
    l->cells = malloc((size_t)cols * sizeof *cells);
    if (!l->cells) { l->cols = 0; return 1; }
    memcpy(l->cells, cells, (size_t)cols * sizeof *cells);
    l->cols = cols;
    if (T.sb_off > 0 && T.sb_off < T.sb_count) T.sb_off++;   /* keep view stable */
    return 1;
}

static int cb_popline(int cols, VTermScreenCell *cells, void *u) {
    (void)cols; (void)cells; (void)u;
    return 0;
}

static int cb_prop(VTermProp prop, VTermValue *val, void *u) {
    (void)u;
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        T.cursor_visible = val->boolean;
        break;
    case VTERM_PROP_ALTSCREEN:
        T.altscreen = val->boolean;
        T.sb_off = 0;
        break;
    case VTERM_PROP_TITLE: {
        VTermStringFragment fr = val->string;
        if (fr.initial) T.title_len = 0;
        size_t n = fr.len;
        if (T.title_len + n >= sizeof T.title) n = sizeof T.title - 1 - T.title_len;
        memcpy(T.title + T.title_len, fr.str, n);
        T.title_len += n;
        if (fr.final) {
            T.title[T.title_len] = 0;
            SetWindowTitle(T.title);
        }
        break;
    }
    default: break;
    }
    return 1;
}

static const VTermScreenCallbacks SCREEN_CB = {
    .settermprop = cb_prop,
    .sb_pushline = cb_pushline,
    .sb_popline  = cb_popline,
};

/* ------------------------------------------------------------------- pty */

static int spawn_shell(int rows, int cols) {
    struct winsize ws = { .ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols };
    pid_t pid = forkpty(&T.master, NULL, NULL, &ws);
    if (pid < 0) return -1;
    if (pid == 0) {
        setenv("TERM", cfg.term, 1);
        setenv("COLORTERM", "truecolor", 1);
        char buf[256];
        snprintf(buf, sizeof buf, "%s", cfg.shell);
        char *argv[32];
        int n = 0;
        for (char *t = strtok(buf, " \t"); t && n < 31; t = strtok(NULL, " \t")) argv[n++] = t;
        argv[n] = NULL;
        if (n == 0) { argv[0] = "/bin/sh"; argv[1] = NULL; }
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    T.child = pid;
    fcntl(T.master, F_SETFL, fcntl(T.master, F_GETFL) | O_NONBLOCK);
    return 0;
}

/* returns >0 if data was read, 0 if idle, -1 if the child is gone */
static int pump(void) {
    static char buf[65536];
    size_t total = 0;
    int got = 0;
    while (total < (1u << 19)) {
        ssize_t n = read(T.master, buf, sizeof buf);
        if (n > 0) { vterm_input_write(T.vt, buf, (size_t)n); total += (size_t)n; got = 1; }
        else if (n == 0) return -1;
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN) break;
        else return -1;
    }
    return got;
}

static void apply_size(int nc, int nr) {
    vterm_set_size(T.vt, nr, nc);
    struct winsize ws = { .ws_row = (unsigned short)nr, .ws_col = (unsigned short)nc };
    ioctl(T.master, TIOCSWINSZ, &ws);
    T.rows = nr; T.cols = nc;
    S.active = false;
    if (T.sb_off > T.sb_count) T.sb_off = T.sb_count;
}

/* ------------------------------------------------------------ cell access */

/* vrow is a viewport row; scrollback is shown when sb_off > 0 */
static bool get_cell(int vrow, int col, VTermScreenCell *cell) {
    int v = vrow - T.sb_off;
    if (v >= 0) {
        VTermPos p = { .row = v, .col = col };
        return vterm_screen_get_cell(T.screen, p, cell) != 0;
    }
    int idx = T.sb_count + v;
    if (idx < 0) return false;
    SbLine *l = sb_line(idx);
    if (col >= l->cols) return false;
    *cell = l->cells[col];
    return true;
}

static Color resolve(VTermColor c, bool is_fg, bool bold) {
    if (is_fg ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c))
        return C(is_fg ? cfg.fg : cfg.bg);
    if (VTERM_COLOR_IS_INDEXED(&c)) {
        int idx = c.indexed.idx;
        if (is_fg && bold && cfg.bold_bright && idx < 8) idx += 8;
        if (idx < 16) return C(cfg.palette[idx]);
        c.indexed.idx = (uint8_t)idx;
    }
    vterm_screen_convert_color_to_rgb(T.screen, &c);
    return (Color){ c.rgb.red, c.rgb.green, c.rgb.blue, 255 };
}

/* -------------------------------------------------------------- selection */

static void sel_range(long *a, long *b) {
    *a = (long)S.r0 * T.cols + S.c0;
    *b = (long)S.r1 * T.cols + S.c1;
    if (*a > *b) { long t = *a; *a = *b; *b = t; }
}

static bool in_sel(int r, int c) {
    if (!S.active) return false;
    long a, b;
    sel_range(&a, &b);
    long p = (long)r * T.cols + c;
    return p >= a && p <= b;
}

static void copy_selection(void) {
    if (!S.active) return;
    long a, b;
    sel_range(&a, &b);
    int ra = (int)(a / T.cols), ca = (int)(a % T.cols);
    int rb = (int)(b / T.cols), cb = (int)(b % T.cols);

    size_t cap = (size_t)(b - a + 1) * 4 * VTERM_MAX_CHARS_PER_CELL + (size_t)T.rows + 2;
    char *buf = malloc(cap);
    if (!buf) return;
    size_t n = 0;

    for (int r = ra; r <= rb; r++) {
        int c0 = (r == ra) ? ca : 0;
        int c1 = (r == rb) ? cb : T.cols - 1;
        size_t start = n;
        for (int c = c0; c <= c1; c++) {
            VTermScreenCell cell;
            if (!get_cell(r, c, &cell) || cell.chars[0] == 0) { buf[n++] = ' '; continue; }
            if (cell.chars[0] == (uint32_t)-1) continue;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++)
                n += (size_t)utf8_enc(cell.chars[i], buf + n);
        }
        while (n > start && buf[n - 1] == ' ') n--;
        if (r < rb) buf[n++] = '\n';
    }
    buf[n] = 0;
    SetClipboardText(buf);
    free(buf);
}

static void paste(void) {
    const char *t = GetClipboardText();
    if (!t || !*t) return;
    size_t len = strlen(t);
    char *b = malloc(len);
    if (!b) return;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (t[i] == '\r' && t[i + 1] == '\n') continue;
        b[n++] = (t[i] == '\n') ? '\r' : t[i];
    }
    vterm_keyboard_start_paste(T.vt);
    write_all(T.master, b, n);
    vterm_keyboard_end_paste(T.vt);
    free(b);
}

/* ------------------------------------------------------------------ input */

static void typed(void) {
    S.active = false;
    T.sb_off = 0;
    g_last_input = GetTime();
}

static void scroll_by(int n) {
    if (T.altscreen) return;
    T.sb_off += n;
    if (T.sb_off < 0) T.sb_off = 0;
    if (T.sb_off > T.sb_count) T.sb_off = T.sb_count;
    S.active = false;
}

static void handle_keyboard(void) {
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT);
    VTermModifier mod = (VTermModifier)((shift ? VTERM_MOD_SHIFT : 0) |
                                        (alt   ? VTERM_MOD_ALT   : 0) |
                                        (ctrl  ? VTERM_MOD_CTRL  : 0));

    bool bound = false;
    if      (bind_hit(&B_copy,  ctrl, shift, alt)) { copy_selection(); bound = true; }
    else if (bind_hit(&B_paste, ctrl, shift, alt)) { typed(); paste(); bound = true; }
    else if (bind_hit(&B_sup,   ctrl, shift, alt)) { scroll_by(T.rows / 2); bound = true; }
    else if (bind_hit(&B_sdown, ctrl, shift, alt)) { scroll_by(-T.rows / 2); bound = true; }

    /* always drain the char queue */
    int ch;
    while ((ch = GetCharPressed())) {
        if (bound || ctrl) continue;
        typed();
        vterm_keyboard_unichar(T.vt, (uint32_t)ch, alt ? VTERM_MOD_ALT : VTERM_MOD_NONE);
    }
    if (bound) return;

    /* special keys */
    static const struct { int rl; VTermKey vk; } km[] = {
        {KEY_ENTER, VTERM_KEY_ENTER}, {KEY_KP_ENTER, VTERM_KEY_ENTER},
        {KEY_TAB, VTERM_KEY_TAB}, {KEY_BACKSPACE, VTERM_KEY_BACKSPACE},
        {KEY_ESCAPE, VTERM_KEY_ESCAPE},
        {KEY_UP, VTERM_KEY_UP}, {KEY_DOWN, VTERM_KEY_DOWN},
        {KEY_LEFT, VTERM_KEY_LEFT}, {KEY_RIGHT, VTERM_KEY_RIGHT},
        {KEY_INSERT, VTERM_KEY_INS}, {KEY_DELETE, VTERM_KEY_DEL},
        {KEY_HOME, VTERM_KEY_HOME}, {KEY_END, VTERM_KEY_END},
        {KEY_PAGE_UP, VTERM_KEY_PAGEUP}, {KEY_PAGE_DOWN, VTERM_KEY_PAGEDOWN},
    };
    for (size_t i = 0; i < sizeof km / sizeof *km; i++)
        if (pressed(km[i].rl)) { typed(); vterm_keyboard_key(T.vt, km[i].vk, mod); }
    for (int i = 0; i < 12; i++)
        if (pressed(KEY_F1 + i)) { typed(); vterm_keyboard_key(T.vt, VTERM_KEY_FUNCTION(i + 1), mod); }

    /* ctrl + letter / a few punctuation keys -> control bytes */
    if (ctrl) {
        for (int k = KEY_A; k <= KEY_Z; k++)
            if (pressed(k)) {
                char seq[2] = { 0x1b, (char)(k - KEY_A + 1) };
                typed();
                if (alt) write_all(T.master, seq, 2); else write_all(T.master, seq + 1, 1);
            }
        static const struct { int rl; char b; } cc[] = {
            {KEY_SPACE, 0}, {KEY_LEFT_BRACKET, 27}, {KEY_BACKSLASH, 28}, {KEY_RIGHT_BRACKET, 29},
        };
        for (size_t i = 0; i < sizeof cc / sizeof *cc; i++)
            if (pressed(cc[i].rl)) { typed(); write_all(T.master, &cc[i].b, 1); }
    }
}

static void handle_mouse(void) {
    Vector2 m = GetMousePosition();
    int c = (int)((m.x - cfg.padding) / g_cw);
    int r = (int)((m.y - cfg.padding) / g_ch);
    if (c < 0) c = 0; if (c >= T.cols) c = T.cols - 1;
    if (r < 0) r = 0; if (r >= T.rows) r = T.rows - 1;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        S.active = true; S.dragging = true;
        S.r0 = S.r1 = r; S.c0 = S.c1 = c;
    } else if (S.dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        S.r1 = r; S.c1 = c;
    }
    if (S.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        S.dragging = false;
        if (S.r0 == S.r1 && S.c0 == S.c1) S.active = false;
        else if (cfg.copy_on_select) copy_selection();
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) { typed(); paste(); }

    float w = GetMouseWheelMove();
    if (w != 0.0f) {
        int n = (int)roundf(w * (float)cfg.scroll_lines);
        if (n == 0) n = w > 0 ? 1 : -1;
        if (T.altscreen) {
            /* full-screen apps: wheel -> arrow keys */
            for (int i = 0; i < abs(n); i++)
                vterm_keyboard_key(T.vt, n > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        } else {
            scroll_by(n);
        }
    }
}

/* ----------------------------------------------------------------- render */

static void render(void) {
    Color bgc0 = C(cfg.bg);
    bgc0.a = (unsigned char)(cfg.opacity * 255.0f);
    ClearBackground(bgc0);

    VTermPos cp;
    vterm_state_get_cursorpos(T.state, &cp);
    bool focused  = IsWindowFocused();
    bool live     = T.cursor_visible && T.sb_off == 0;
    bool blink_on = true;
    double now = GetTime();
    if (cfg.cursor_blink && now - g_last_input > cfg.cursor_blink_ms / 1000.0)
        blink_on = ((long)(now * 1000.0) / cfg.cursor_blink_ms) % 2 == 0;
    bool cur_on     = live && focused && blink_on;
    bool block_cur  = cur_on && g_cursor_style == 0;

    for (int r = 0; r < T.rows; r++) {
        for (int c = 0; c < T.cols; c++) {
            VTermScreenCell cell;
            if (!get_cell(r, c, &cell)) continue;
            if (cell.chars[0] == (uint32_t)-1) continue;   /* right half of wide char */

            int  w    = cell.width > 0 ? cell.width : 1;
            bool bold = cell.attrs.bold;
            Color fg  = resolve(cell.fg, true, bold);
            Color bg  = resolve(cell.bg, false, false);
            bool bg_default = VTERM_COLOR_IS_DEFAULT_BG(&cell.bg);

            if (cell.attrs.reverse) { Color t = fg; fg = bg; bg = t; bg_default = false; }
            if (in_sel(r, c))       { fg = C(cfg.selection_fg); bg = C(cfg.selection_bg); bg_default = false; }
            if (block_cur && live && r == cp.row && c == cp.col) {
                bg = C(cfg.cursor_color); fg = C(cfg.bg); bg_default = false;
            }

            int x = cfg.padding + c * g_cw;
            int y = cfg.padding + r * g_ch;
            if (!bg_default) DrawRectangle(x, y, w * g_cw, g_ch, bg);

            Vector2 p = { (float)x, (float)y + (float)(g_ch - g_fs) / 2.0f };
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) {
                uint32_t cpnt = cell.chars[i];
                if (cpnt == ' ' || cpnt < 32) continue;
                DrawTextCodepoint(g_font, (int)cpnt, p, (float)g_fs, fg);
                if (bold && cfg.fake_bold)
                    DrawTextCodepoint(g_font, (int)cpnt, (Vector2){ p.x + 1, p.y }, (float)g_fs, fg);
            }
            if (cell.attrs.underline) DrawRectangle(x, y + g_ch - 1, w * g_cw, 1, fg);
            if (cell.attrs.strike)    DrawRectangle(x, y + g_ch / 2, w * g_cw, 1, fg);
        }
    }

    if (live) {
        int x = cfg.padding + cp.col * g_cw;
        int y = cfg.padding + cp.row * g_ch;
        Color cc = C(cfg.cursor_color);
        int th = cfg.cursor_thickness;
        if (!focused)                         DrawRectangleLines(x, y, g_cw, g_ch, cc);
        else if (cur_on && g_cursor_style == 1) DrawRectangle(x, y + g_ch - th, g_cw, th, cc);
        else if (cur_on && g_cursor_style == 2) DrawRectangle(x, y, th, g_ch, cc);
    }
}

/* ------------------------------------------------------------------- font */

static Font load_font(void) {
    int  cps[4096];
    int  n = 0;
    static const int ranges[][2] = {
        {32, 126}, {160, 0x24F}, {0x370, 0x3FF}, {0x400, 0x4FF}, {0x2000, 0x206F},
        {0x2190, 0x21FF}, {0x2200, 0x22FF}, {0x2500, 0x259F}, {0x25A0, 0x25FF},
        {0x2800, 0x28FF}, {0xFFFD, 0xFFFD},
    };
    for (size_t i = 0; i < sizeof ranges / sizeof *ranges; i++)
        for (int c = ranges[i][0]; c <= ranges[i][1] && n < 4096; c++) cps[n++] = c;

    const char *cands[] = {
        cfg.font_path,
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof cands / sizeof *cands; i++) {
        if (!cands[i][0] || !FileExists(cands[i])) continue;
        Font f = LoadFontEx(cands[i], g_fs, cps, n);
        if (f.texture.id != 0 && f.texture.id != GetFontDefault().texture.id) {
            SetTextureFilter(f.texture, cfg.font_smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
            return f;
        }
    }
    fprintf(stderr, "silly-term: no usable font found, falling back to raylib default (set font_path)\n");
    return GetFontDefault();
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    const char *conf = NULL;
    bool explicit_conf = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) { conf = argv[++i]; explicit_conf = true; }
        else { fprintf(stderr, "usage: %s [-c config-file]\n", argv[0]); return strcmp(argv[i], "-h") ? 1 : 0; }
    }

    char path[512];
    if (!conf && default_conf_path(path, sizeof path) == 0) conf = path;
    if (conf) {
        if (load_conf(conf, &cfg) != 0 && explicit_conf)
            fprintf(stderr, "silly-term: cannot open %s, using defaults\n", conf);
    } else {
        set_defaults(&cfg);
    }

    g_cursor_style = !strcmp(cfg.cursor_style, "underline") ? 1 : !strcmp(cfg.cursor_style, "bar") ? 2 : 0;
    B_copy  = parse_bind(cfg.key_copy);
    B_paste = parse_bind(cfg.key_paste);
    B_sup   = parse_bind(cfg.key_scroll_up);
    B_sdown = parse_bind(cfg.key_scroll_down);

    /* window (hidden until we know the real size) */
    unsigned flags = FLAG_WINDOW_HIDDEN;
    if (!cfg.decorated)    flags |= FLAG_WINDOW_UNDECORATED;
    if (cfg.resizable)     flags |= FLAG_WINDOW_RESIZABLE;
    if (cfg.opacity < 1.f) flags |= FLAG_WINDOW_TRANSPARENT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(100, 100, cfg.title);
    SetExitKey(KEY_NULL);                    /* ESC must reach the shell */

    g_fs   = cfg.font_size;
    g_font = load_font();
    Vector2 m = MeasureTextEx(g_font, "M", (float)g_fs, 0);
    g_cw = (int)ceilf(m.x) + cfg.letter_spacing;
    g_ch = (int)ceilf((float)g_fs * cfg.line_height);
    if (g_cw < 1) g_cw = 1;
    if (g_ch < 1) g_ch = 1;

    T.rows = cfg.rows;
    T.cols = cfg.cols;
    SetWindowSize(cfg.cols * g_cw + 2 * cfg.padding, cfg.rows * g_ch + 2 * cfg.padding);
    if (cfg.resizable) SetWindowMinSize(2 * g_cw + 2 * cfg.padding, g_ch + 2 * cfg.padding);
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetTargetFPS(cfg.fps);

    /* terminal core */
    if (cfg.scrollback > 0) {
        T.sb = calloc((size_t)cfg.scrollback, sizeof *T.sb);
        T.sb_cap = T.sb ? cfg.scrollback : 0;
    }
    T.vt = vterm_new(T.rows, T.cols);
    vterm_set_utf8(T.vt, 1);
    T.screen = vterm_obtain_screen(T.vt);
    T.state  = vterm_obtain_state(T.vt);
    vterm_output_set_callback(T.vt, out_cb, NULL);
    vterm_screen_set_callbacks(T.screen, &SCREEN_CB, NULL);
    vterm_screen_enable_altscreen(T.screen, 1);
    vterm_screen_reset(T.screen, 1);

    if (spawn_shell(T.rows, T.cols) != 0) {
        perror("forkpty");
        return 1;
    }

    /* main loop */
    while (!WindowShouldClose()) {
        if (cfg.resizable && IsWindowResized()) {
            int nc = (GetScreenWidth()  - 2 * cfg.padding) / g_cw;
            int nr = (GetScreenHeight() - 2 * cfg.padding) / g_ch;
            if (nc < 1) nc = 1;
            if (nr < 1) nr = 1;
            if (nc != T.cols || nr != T.rows) apply_size(nc, nr);
        }

        handle_keyboard();
        handle_mouse();

        int got = pump();
        if (got < 0) break;                          /* shell exited */
        if (got > 0 && !S.dragging) S.active = false;

        BeginDrawing();
        render();
        EndDrawing();
    }

    /* cleanup */
    if (T.master >= 0) close(T.master);
    if (T.child > 0) { kill(T.child, SIGHUP); waitpid(T.child, NULL, WNOHANG); }
    for (int i = 0; i < T.sb_count; i++) free(sb_line(i)->cells);
    free(T.sb);
    vterm_free(T.vt);
    if (g_font.texture.id != GetFontDefault().texture.id) UnloadFont(g_font);
    CloseWindow();
    return 0;
}