/*
 * optimizations applied
 *  - glyph lookup table (ascii direct + binary search): raylib's
 *    GetGlyphIndex is a linear scan over every loaded glyph, per cell
 *  - glyphs drawn with DrawTexturePro using the cached index/scale
 *  - cached config colors and 256-color palette in View (no rgb()/vterm
 *    conversion in the cell loop)
 *  - resolve_fg/resolve_bg split, DEFAULT -> INDEXED -> RGB order
 *  - block cursor drawn after the grid pass (no per-cell cursor test)
 *  - 1-codepoint fast path, combining chars only when chars[1] != 0
 *  - selection tested only when sel != NULL && sel->active
 *  - underline/strike merged into one rect per contiguous same-color run
 *  - blink guarded against cursor_blink_ms <= 0; cps truncation is logged
 *
 * deliberately postponed
 *  - two-pass bg/glyph batching: needs a per-row cell buffer owned by View
 *    (no alloc allowed in render_frame); measure draw calls first
 *  - fake bold texture cache: not measured yet, behavior kept as is
 *  - configurable font ranges: needs new Config fields (see TODO)
 *  - skipping frames with no damage: belongs in the main loop, not here
 */
#include "headers/render.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------- colors */

static inline Color rgb(Rgb c) {
    return (Color){ c.r, c.g, c.b, 255 };
}

static inline Color vterm_rgb(VTermColor c) {
    return (Color){ c.rgb.red, c.rgb.green, c.rgb.blue, 255 };
}

static inline bool color_matches_rgb(Color a, Rgb b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == 255;
}

static inline bool color_eq(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void update_config_colors(View *v, const Config *cfg) {
    v->col_fg     = rgb(cfg->fg);
    v->col_bg     = rgb(cfg->bg);
    v->col_sel_fg = rgb(cfg->selection_fg);
    v->col_sel_bg = rgb(cfg->selection_bg);
    v->col_cursor = rgb(cfg->cursor_color);

    for (int i = 0; i < 16; ++i)
        v->pal256[i] = rgb(cfg->palette[i]);
}

static void update_vterm_palette(View *v, const Term *t) {
    for (int i = 16; i < 256; ++i) {
        VTermColor color;
        vterm_color_indexed(&color, (uint8_t)i);
        vterm_screen_convert_color_to_rgb(t->screen, &color);
        v->pal256[i] = vterm_rgb(color);
    }

    v->palette_screen = t->screen;
    v->palette_ready = true;
}

static inline bool config_colors_changed(const View *v, const Config *cfg) {
    if (!color_matches_rgb(v->col_fg, cfg->fg) ||
        !color_matches_rgb(v->col_bg, cfg->bg) ||
        !color_matches_rgb(v->col_sel_fg, cfg->selection_fg) ||
        !color_matches_rgb(v->col_sel_bg, cfg->selection_bg) ||
        !color_matches_rgb(v->col_cursor, cfg->cursor_color))
        return true;

    for (int i = 0; i < 16; ++i)
        if (!color_matches_rgb(v->pal256[i], cfg->palette[i]))
            return true;

    return false;
}

static inline Color resolve_fg(const View *v, const Config *cfg,
                               VTermColor color, bool bold) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color))
        return v->col_fg;

    if (VTERM_COLOR_IS_INDEXED(&color)) {
        unsigned int idx = color.indexed.idx;
        if (bold && cfg->bold_bright && idx < 8)
            idx += 8;
        return v->pal256[idx];
    }

    return vterm_rgb(color);
}

static inline Color resolve_bg(const View *v, const Config *cfg,
                               VTermColor color) {
    (void)cfg;

    if (VTERM_COLOR_IS_DEFAULT_BG(&color))
        return v->col_bg;

    if (VTERM_COLOR_IS_INDEXED(&color))
        return v->pal256[color.indexed.idx];

    return vterm_rgb(color);
}

/* ------------------------------------------------------------------- font */

static Font load_font(const Config *cfg, int fs) {
    /* TODO: make ranges configurable (e.g. cfg->load_braille) once Config
       grows such fields; default must keep every range below. */
    int cps[4096];
    int n = 0;
    static const int ranges[][2] = {
        {32, 126}, {160, 0x24F}, {0x370, 0x3FF}, {0x400, 0x4FF}, {0x2000, 0x206F},
        {0x2190, 0x21FF}, {0x2200, 0x22FF}, {0x2500, 0x259F}, {0x25A0, 0x25FF},
        {0x2800, 0x28FF}, {0xFFFD, 0xFFFD},
    };

    for (size_t i = 0; i < sizeof ranges / sizeof *ranges; ++i) {
        for (int c = ranges[i][0]; c <= ranges[i][1]; ++c) {
            if (n == (int)(sizeof cps / sizeof *cps)) {
                fprintf(stderr,
                        "silly-term: font codepoint list truncated at %zu entries\n",
                        sizeof cps / sizeof *cps);
                goto ranges_done;
            }
            cps[n++] = c;
        }
    }
ranges_done:

    const char *cands[] = {
        cfg->font_path,
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof cands / sizeof *cands; ++i) {
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

/* glyphs are loaded in ascending codepoint order, so ascii gets a direct
   table and everything else can be binary searched */
static void build_glyph_index(View *v) {
    v->fallback_idx = 0;
    for (int i = 0; i < v->font.glyphCount; ++i)
        if (v->font.glyphs[i].value == '?') { v->fallback_idx = i; break; }

    for (int i = 0; i < 128; ++i)
        v->ascii_idx[i] = v->fallback_idx;

    for (int i = 0; i < v->font.glyphCount; ++i) {
        const int val = v->font.glyphs[i].value;
        if (val >= 0 && val < 128)
            v->ascii_idx[val] = i;
    }
}

int view_init(View *v, const Config *cfg) {
    memset(v, 0, sizeof *v);
    v->fs   = cfg->font_size;
    v->font = load_font(cfg, v->fs);
    measure(v, cfg);
    build_glyph_index(v);
    update_config_colors(v, cfg);

    v->cursor_style = !strcmp(cfg->cursor_style, "underline") ? 1
                    : !strcmp(cfg->cursor_style, "bar")       ? 2 : 0;
    return 0;
}

void view_free(View *v) {
    if (v->font.texture.id != 0 && v->font.texture.id != GetFontDefault().texture.id)
        UnloadFont(v->font);
    v->font.texture.id = 0;
}

void view_update_colors(View *v, const Config *cfg, const Term *t) {
    update_config_colors(v, cfg);
    update_vterm_palette(v, t);
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
    build_glyph_index(v);
    return 1;
}

int view_pixel_width(const View *v, const Config *cfg, int cols)  { return cols * v->cw + 2 * cfg->padding; }
int view_pixel_height(const View *v, const Config *cfg, int rows) { return rows * v->ch + 2 * cfg->padding; }

/* ------------------------------------------------------------------ drawing */

static inline int glyph_index(const View *v, uint32_t cpnt) {
    if (cpnt < 128)
        return v->ascii_idx[cpnt];
    if (cpnt > 0x10FFFF)
        return v->fallback_idx;

    const int key = (int)cpnt;
    int lo = 0;
    int hi = v->font.glyphCount - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const int val = v->font.glyphs[mid].value;
        if (val == key) return mid;
        if (val < key)  lo = mid + 1;
        else            hi = mid - 1;
    }
    return v->fallback_idx;
}

/* same math as raylib's DrawTextCodepoint, minus the linear glyph search */
static inline void draw_glyph(const View *v, int idx, Vector2 pos,
                              float scale, Color fg) {
    const Font *f = &v->font;
    const Rectangle r = f->recs[idx];
    const GlyphInfo *g = &f->glyphs[idx];
    const float pad = (float)f->glyphPadding;

    const Rectangle src = { r.x - pad, r.y - pad,
                            r.width + 2.0f * pad, r.height + 2.0f * pad };
    const Rectangle dst = { pos.x + (float)g->offsetX * scale - pad * scale,
                            pos.y + (float)g->offsetY * scale - pad * scale,
                            (r.width + 2.0f * pad) * scale,
                            (r.height + 2.0f * pad) * scale };

    DrawTexturePro(f->texture, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, fg);
}

static inline void draw_codepoint(const View *v, uint32_t cpnt, Vector2 pos,
                                  float scale, Color fg, bool bold, bool fake_bold) {
    if (cpnt == ' ' || cpnt < 32)
        return;

    const int idx = glyph_index(v, cpnt);
    draw_glyph(v, idx, pos, scale, fg);
    /* fake bold: 2x draw por célula bold */
    if (bold && fake_bold)
        draw_glyph(v, idx, (Vector2){ pos.x + 1.0f, pos.y }, scale, fg);
}

static inline void draw_cell_text(const View *v, const VTermScreenCell *cell,
                                  Vector2 pos, float scale, Color fg,
                                  bool bold, bool fake_bold) {
    uint32_t first = cell->chars[0];
    if (!first)
        return;

    draw_codepoint(v, first, pos, scale, fg, bold, fake_bold);

    /* Most terminal cells contain one codepoint. Only scan the tail when needed. */
    if (cell->chars[1]) {
        for (int i = 1; i < VTERM_MAX_CHARS_PER_CELL && cell->chars[i]; ++i)
            draw_codepoint(v, cell->chars[i], pos, scale, fg, bold, fake_bold);
    }
}

/* horizontal decoration run (underline/strike): one rect per same-color run */
typedef struct {
    int   x, y, w;
    Color col;
    bool  on;
} Run;

static inline void run_flush(Run *r) {
    if (r->on)
        DrawRectangle(r->x, r->y, r->w, 1, r->col);
    r->on = false;
}

static inline void run_add(Run *r, int x, int y, int w, Color col) {
    if (r->on && x == r->x + r->w && color_eq(col, r->col)) {
        r->w += w;
        return;
    }
    run_flush(r);
    r->x = x;
    r->y = y;
    r->w = w;
    r->col = col;
    r->on = true;
}

bool render_blink_on(const Config *cfg, double last_input) {
    if (!cfg->cursor_blink || cfg->cursor_blink_ms <= 0)
        return true;

    const double now = GetTime();
    if (now - last_input <= cfg->cursor_blink_ms / 1000.0)
        return true;

    return ((long)(now * 1000.0) / cfg->cursor_blink_ms) % 2 == 0;
}

/* ------------------------------------------------------------------ frame */

void render_frame(View *v, const Config *cfg, const Term *t,
                  const Selection *sel, double last_input) {
    /* Config colors may be reloaded at runtime; compare only cached values here. */
    if (config_colors_changed(v, cfg))
        update_config_colors(v, cfg);

    /* view_init has no Term parameter, so resolve 16..255 once the screen exists. */
    if (!v->palette_ready || v->palette_screen != t->screen)
        update_vterm_palette(v, t);

    Color clear = v->col_bg;
    clear.a = (unsigned char)(cfg->opacity * 255.0f);
    ClearBackground(clear);

    VTermPos cp;
    vterm_state_get_cursorpos(t->state, &cp);

    const bool focused = IsWindowFocused();
    const bool live = t->cursor_visible && t->sb_off == 0;
    const bool blink_on = render_blink_on(cfg, last_input);

    const bool cur_on = live && focused && blink_on;
    const bool block_cur = cur_on && v->cursor_style == 0;
    const bool has_sel = sel != NULL && sel->active;
    const bool fake_bold = cfg->fake_bold;
    const float scale = v->font.baseSize > 0
                      ? (float)v->fs / (float)v->font.baseSize : 1.0f;
    const float y_offset = (float)(v->ch - v->fs) / 2.0f;
    const int padding = cfg->padding;
    const int cw = v->cw;
    const int ch = v->ch;

    Run ul = { 0, 0, 0, { 0, 0, 0, 0 }, false };
    Run st = { 0, 0, 0, { 0, 0, 0, 0 }, false };

    for (int r = 0; r < t->rows; ++r) {
        const int y = padding + r * ch;
        for (int c = 0; c < t->cols; ++c) {
            VTermScreenCell cell;
            if (!term_get_cell(t, r, c, &cell)) continue;
            if (cell.chars[0] == (uint32_t)-1) continue; /* right half of wide char */

            const int w = cell.width > 0 ? cell.width : 1;
            const bool bold = cell.attrs.bold;
            Color fg = resolve_fg(v, cfg, cell.fg, bold);
            Color bg = resolve_bg(v, cfg, cell.bg);
            bool bg_default = VTERM_COLOR_IS_DEFAULT_BG(&cell.bg);

            if (cell.attrs.reverse) {
                const Color tmp = fg;
                fg = bg;
                bg = tmp;
                bg_default = false;
            }
            if (has_sel && sel_contains(sel, t, r, c)) {
                fg = v->col_sel_fg;
                bg = v->col_sel_bg;
                bg_default = false;
            }

            const int x = padding + c * cw;
            if (!bg_default)
                DrawRectangle(x, y, w * cw, ch, bg);

            const Vector2 pos = { (float)x, (float)y + y_offset };
            draw_cell_text(v, &cell, pos, scale, fg, bold, fake_bold);

            if (cell.attrs.underline) run_add(&ul, x, y + ch - 1, w * cw, fg);
            if (cell.attrs.strike)    run_add(&st, x, y + ch / 2, w * cw, fg);
        }
        run_flush(&ul);
        run_flush(&st);
    }

    /* The block cursor overlays the cell after the normal grid pass. */
    if (block_cur && cp.row >= 0 && cp.row < t->rows &&
        cp.col >= 0 && cp.col < t->cols) {
        VTermScreenCell cell;
        if (term_get_cell(t, cp.row, cp.col, &cell) &&
            cell.chars[0] != (uint32_t)-1) {
            const int w = cell.width > 0 ? cell.width : 1;
            const int x = padding + cp.col * cw;
            const int y = padding + cp.row * ch;
            const Vector2 pos = { (float)x, (float)y + y_offset };
            const bool bold = cell.attrs.bold;

            DrawRectangle(x, y, w * cw, ch, v->col_cursor);
            draw_cell_text(v, &cell, pos, scale, v->col_bg, bold, fake_bold);

            if (cell.attrs.underline)
                DrawRectangle(x, y + ch - 1, w * cw, 1, v->col_bg);
            if (cell.attrs.strike)
                DrawRectangle(x, y + ch / 2, w * cw, 1, v->col_bg);
        }
    }

    if (live) {
        const int x = padding + cp.col * cw;
        const int y = padding + cp.row * ch;
        const int th = cfg->cursor_thickness;

        if (!focused)                            DrawRectangleLines(x, y, cw, ch, v->col_cursor);
        else if (cur_on && v->cursor_style == 1) DrawRectangle(x, y + ch - th, cw, th, v->col_cursor);
        else if (cur_on && v->cursor_style == 2) DrawRectangle(x, y, th, ch, v->col_cursor);
    }
}