#include "headers/render.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------- font */

static Font load_font(const Config *cfg, int fs) {
    int cps[4096];
    int n = 0;
    static const int ranges[][2] = {
        {32, 126}, {160, 0x24F}, {0x370, 0x3FF}, {0x400, 0x4FF}, {0x2000, 0x206F},
        {0x2190, 0x21FF}, {0x2200, 0x22FF}, {0x2500, 0x259F}, {0x25A0, 0x25FF},
        {0x2800, 0x28FF}, {0xFFFD, 0xFFFD},
    };
    for (size_t i = 0; i < sizeof ranges / sizeof *ranges; i++)
        for (int c = ranges[i][0]; c <= ranges[i][1] && n < 4096; c++) cps[n++] = c;

    const char *cands[] = {
        cfg->font_path,
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof cands / sizeof *cands; i++) {
        if (!cands[i][0] || !FileExists(cands[i])) continue;
        Font f = LoadFontEx(cands[i], fs, cps, n);
        if (f.texture.id != 0 && f.texture.id != GetFontDefault().texture.id) {
            SetTextureFilter(f.texture, cfg->font_smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
            return f;
        }
    }
    fprintf(stderr, "silly-term: no usable font found, using raylib default (set font_path)\n");
    return GetFontDefault();
}

static void measure(View *v, const Config *cfg) {
    Vector2 m = MeasureTextEx(v->font, "M", (float)v->fs, 0);
    v->cw = (int)ceilf(m.x) + cfg->letter_spacing;
    v->ch = (int)ceilf((float)v->fs * cfg->line_height);
    if (v->cw < 1) v->cw = 1;
    if (v->ch < 1) v->ch = 1;
}

int view_init(View *v, const Config *cfg) {
    memset(v, 0, sizeof *v);
    v->fs   = cfg->font_size;
    v->font = load_font(cfg, v->fs);
    measure(v, cfg);

    v->cursor_style = !strcmp(cfg->cursor_style, "underline") ? 1
                    : !strcmp(cfg->cursor_style, "bar")       ? 2 : 0;
    return 0;
}

void view_free(View *v) {
    if (v->font.texture.id != 0 && v->font.texture.id != GetFontDefault().texture.id)
        UnloadFont(v->font);
    v->font.texture.id = 0;
}

int view_set_font_size(View *v, const Config *cfg, int size) {
    if (size < 6)   size = 6;
    if (size > 200) size = 200;
    if (size == v->fs) return 0;

    Font nf = load_font(cfg, size);          /* load first so a failure keeps the old font */
    view_free(v);
    v->font = nf;
    v->fs   = size;
    measure(v, cfg);
    return 1;
}

int view_pixel_width(const View *v, const Config *cfg, int cols)  { return cols * v->cw + 2 * cfg->padding; }
int view_pixel_height(const View *v, const Config *cfg, int rows) { return rows * v->ch + 2 * cfg->padding; }

/* ----------------------------------------------------------------- colors */

static Color rgb(Rgb c) { return (Color){ c.r, c.g, c.b, 255 }; }

static Color resolve(const Config *cfg, const Term *t, VTermColor c, bool is_fg, bool bold) {
    if (is_fg ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c))
        return rgb(is_fg ? cfg->fg : cfg->bg);

    if (VTERM_COLOR_IS_INDEXED(&c)) {
        int idx = c.indexed.idx;
        if (is_fg && bold && cfg->bold_bright && idx < 8) idx += 8;
        if (idx < 16) return rgb(cfg->palette[idx]);
        c.indexed.idx = (uint8_t)idx;
    }
    vterm_screen_convert_color_to_rgb(t->screen, &c);
    return (Color){ c.rgb.red, c.rgb.green, c.rgb.blue, 255 };
}

/* ------------------------------------------------------------------ frame */

void render_frame(const View *v, const Config *cfg, const Term *t,
                  const Selection *sel, double last_input) {
    Color clear = rgb(cfg->bg);
    clear.a = (unsigned char)(cfg->opacity * 255.0f);
    ClearBackground(clear);

    VTermPos cp;
    vterm_state_get_cursorpos(t->state, &cp);

    bool   focused = IsWindowFocused();
    bool   live    = t->cursor_visible && t->sb_off == 0;
    bool   blink_on = true;
    double now     = GetTime();
    if (cfg->cursor_blink && now - last_input > cfg->cursor_blink_ms / 1000.0)
        blink_on = ((long)(now * 1000.0) / cfg->cursor_blink_ms) % 2 == 0;

    bool cur_on    = live && focused && blink_on;
    bool block_cur = cur_on && v->cursor_style == 0;

    for (int r = 0; r < t->rows; r++) {
        for (int c = 0; c < t->cols; c++) {
            VTermScreenCell cell;
            if (!term_get_cell(t, r, c, &cell)) continue;
            if (cell.chars[0] == (uint32_t)-1) continue;       /* right half of wide char */

            int  w    = cell.width > 0 ? cell.width : 1;
            bool bold = cell.attrs.bold;
            Color fg  = resolve(cfg, t, cell.fg, true, bold);
            Color bg  = resolve(cfg, t, cell.bg, false, false);
            bool bg_default = VTERM_COLOR_IS_DEFAULT_BG(&cell.bg);

            if (cell.attrs.reverse) { Color tmp = fg; fg = bg; bg = tmp; bg_default = false; }
            if (sel_contains(sel, t, r, c)) {
                fg = rgb(cfg->selection_fg);
                bg = rgb(cfg->selection_bg);
                bg_default = false;
            }
            if (block_cur && r == cp.row && c == cp.col) {
                bg = rgb(cfg->cursor_color);
                fg = rgb(cfg->bg);
                bg_default = false;
            }

            int x = cfg->padding + c * v->cw;
            int y = cfg->padding + r * v->ch;
            if (!bg_default) DrawRectangle(x, y, w * v->cw, v->ch, bg);

            Vector2 p = { (float)x, (float)y + (float)(v->ch - v->fs) / 2.0f };
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) {
                uint32_t cpnt = cell.chars[i];
                if (cpnt == ' ' || cpnt < 32) continue;
                DrawTextCodepoint(v->font, (int)cpnt, p, (float)v->fs, fg);
                if (bold && cfg->fake_bold)
                    DrawTextCodepoint(v->font, (int)cpnt, (Vector2){ p.x + 1, p.y }, (float)v->fs, fg);
            }
            if (cell.attrs.underline) DrawRectangle(x, y + v->ch - 1, w * v->cw, 1, fg);
            if (cell.attrs.strike)    DrawRectangle(x, y + v->ch / 2, w * v->cw, 1, fg);
        }
    }

    if (live) {
        int x  = cfg->padding + cp.col * v->cw;
        int y  = cfg->padding + cp.row * v->ch;
        int th = cfg->cursor_thickness;
        Color cc = rgb(cfg->cursor_color);

        if (!focused)                            DrawRectangleLines(x, y, v->cw, v->ch, cc);
        else if (cur_on && v->cursor_style == 1) DrawRectangle(x, y + v->ch - th, v->cw, th, cc);
        else if (cur_on && v->cursor_style == 2) DrawRectangle(x, y, th, v->ch, cc);
    }
}