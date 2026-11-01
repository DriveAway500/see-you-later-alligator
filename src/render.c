/*
 * OpenGL 3.3 renderer.
 *
 *  - glyphs are rasterised lazily by FreeType into one 8-bit atlas (glyph.c)
 *  - every rectangle (cell background, glyph, underline, cursor) is a 16-byte
 *    instance of the same unit quad, so the whole grid is a single
 *    glDrawArraysInstanced call and a second one for the cursor
 *  - horizontally adjacent cells with the same background / decoration are
 *    merged into one quad
 *  - each row keeps its quads; only rows damaged since the last frame are
 *    rebuilt (libvterm damage + selection changes), and the VBO is only
 *    re-uploaded when something was rebuilt. A cursor blink re-uploads
 *    a few dozen bytes
 *  - nothing here runs unless main decides a frame is needed
 */
#include "headers/render.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "headers/gl.h"
#include "headers/image.h"

_Static_assert(sizeof(Quad) == 16, "Quad must be tightly packed");

/* ------------------------------------------------------------------ colors */

static inline Rgba rgb(Rgb c)         { return (Rgba){ c.r, c.g, c.b, 255 }; }
static inline Rgba vterm_rgb(VTermColor c) { return (Rgba){ c.rgb.red, c.rgb.green, c.rgb.blue, 255 }; }
static inline bool rgba_eq(Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

static void update_config_colors(View *v, const Config *cfg) {
    v->col_fg     = rgb(cfg->fg);
    v->col_bg     = rgb(cfg->bg);
    v->col_sel_fg = rgb(cfg->selection_fg);
    v->col_sel_bg = rgb(cfg->selection_bg);
    v->col_cursor = rgb(cfg->cursor_color);
    for (int i = 0; i < 16; ++i) v->pal256[i] = rgb(cfg->palette[i]);
}

static void update_vterm_palette(View *v, const Term *t) {
    for (int i = 16; i < 256; ++i) {
        VTermColor color;
        vterm_color_indexed(&color, (uint8_t)i);
        vterm_screen_convert_color_to_rgb(t->screen, &color);
        v->pal256[i] = vterm_rgb(color);
    }
    v->palette_screen = t->screen;
    v->palette_ready  = true;
}

void view_update_colors(View *v, const Config *cfg, const Term *t) {
    update_config_colors(v, cfg);
    update_vterm_palette(v, t);
    v->all_dirty = true;
}

static inline Rgba resolve_fg(const View *v, const Config *cfg, VTermColor color, bool bold) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) return v->col_fg;
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        unsigned idx = color.indexed.idx;
        if (bold && cfg->bold_bright && idx < 8) idx += 8;
        return v->pal256[idx];
    }
    return vterm_rgb(color);
}

static inline Rgba resolve_bg(const View *v, VTermColor color) {
    if (VTERM_COLOR_IS_DEFAULT_BG(&color)) return v->col_bg;
    if (VTERM_COLOR_IS_INDEXED(&color))    return v->pal256[color.indexed.idx];
    return vterm_rgb(color);
}

/* ------------------------------------------------------------------ quads */

static bool qpush(QBuf *b, Quad q) {
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 64;
        Quad *nq = realloc(b->q, (size_t)nc * sizeof *nq);
        if (!nq) return false;
        b->q = nq;
        b->cap = nc;
    }
    b->q[b->n++] = q;
    return true;
}

static inline Quad solid(int x, int y, int w, int h, Rgba c) {
    return (Quad){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, QUAD_SOLID, 0, c };
}

/* horizontal run of same-coloured solid quads (backgrounds, underline, strike) */
typedef struct { int x, w; Rgba col; bool on; } Run;

static inline void run_flush(Run *r, QBuf *out, int y, int h) {
    if (r->on) qpush(out, solid(r->x, y, r->w, h, r->col));
    r->on = false;
}

static inline void run_add(Run *r, QBuf *out, int y, int h, int x, int w, Rgba col) {
    if (r->on && x == r->x + r->w && rgba_eq(col, r->col)) { r->w += w; return; }
    run_flush(r, out, y, h);
    r->x = x; r->w = w; r->col = col; r->on = true;
}

/* all codepoints of a cell (base + combining), optional fake bold */
static void emit_cell_glyphs(View *v, QBuf *out, const VTermScreenCell *cell,
                             int x, int y, Rgba fg, bool fake_bold) {
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell->chars[i]; ++i) {
        Glyph g = glyphs_get(&v->gc, cell->chars[i]);
        if (!g.w) continue;
        Quad q = { (int16_t)(x + g.x), (int16_t)(y + g.y), (int16_t)g.w, (int16_t)g.h, g.u, g.v, fg };
        qpush(out, q);
        if (fake_bold) { q.x++; qpush(out, q); }
    }
}

/* ------------------------------------------------------------- row cache */

static void rows_free(View *v) {
    for (int i = 0; i < v->nrows; i++) { free(v->rows[i].bg.q); free(v->rows[i].fg.q); }
    free(v->rows);
    v->rows = NULL;
    v->nrows = 0;
}

static bool rows_resize(View *v, int n) {
    rows_free(v);
    v->rows = calloc((size_t)n, sizeof *v->rows);
    if (!v->rows) return false;
    v->nrows = n;
    v->all_dirty = true;
    v->main_stale = true;
    return true;
}

static void build_row(View *v, const Config *cfg, const Term *t,
                      bool has_sel, long sa, long sb, int r) {
    RowCache *rc = &v->rows[r];
    rc->bg.n = rc->fg.n = 0;

    const int n  = term_get_row(t, r, v->row_buf, t->cols);   /* cells past n are blank */
    const int cw = v->cw, ch = v->ch;
    const int y  = cfg->padding + r * ch;
    const bool fake_bold = cfg->fake_bold;

    Run bgr = { 0 }, ul = { 0 }, st = { 0 };

    for (int c = 0; c < n; ++c) {
        const VTermScreenCell *cell = &v->row_buf[c];
        if (cell->chars[0] == (uint32_t)-1) continue;           /* right half of a wide char */

        const int  w    = cell->width > 0 ? cell->width : 1;
        const bool bold = cell->attrs.bold;
        const bool in_sel = has_sel && (long)r * t->cols + c >= sa && (long)r * t->cols + c <= sb;
        const bool blank_text = cell->chars[0] == 0 || cell->chars[0] == ' ';

        if (!in_sel && blank_text && VTERM_COLOR_IS_DEFAULT_BG(&cell->bg) &&
            !cell->attrs.reverse && !cell->attrs.underline && !cell->attrs.strike)
            continue;                                           /* default background: nothing to draw */

        Rgba fg = resolve_fg(v, cfg, cell->fg, bold);
        Rgba bg = resolve_bg(v, cell->bg);
        bool bg_default = VTERM_COLOR_IS_DEFAULT_BG(&cell->bg);

        if (cell->attrs.reverse) { Rgba tmp = fg; fg = bg; bg = tmp; bg_default = false; }
        if (in_sel)              { fg = v->col_sel_fg; bg = v->col_sel_bg; bg_default = false; }

        const int x = cfg->padding + c * cw;
        if (!bg_default) run_add(&bgr, &rc->bg, y, ch, x, w * cw, bg);

        if (!blank_text) emit_cell_glyphs(v, &rc->fg, cell, x, y, fg, bold && fake_bold);

        if (cell->attrs.underline) run_add(&ul, &rc->fg, y + ch - 1, 1, x, w * cw, fg);
        if (cell->attrs.strike)    run_add(&st, &rc->fg, y + ch / 2, 1, x, w * cw, fg);
    }
    run_flush(&bgr, &rc->bg, y, ch);
    run_flush(&ul,  &rc->fg, y + ch - 1, 1);
    run_flush(&st,  &rc->fg, y + ch / 2, 1);
}

/* ------------------------------------------------------------------ cursor */

bool render_blink_on(const Config *cfg, double last_input, double now) {
    if (!cfg->cursor_blink || cfg->cursor_blink_ms <= 0) return true;
    const double el = now - last_input;
    if (el < 0) return true;
    if (cfg->cursor_blink_timeout > 0 && el >= cfg->cursor_blink_timeout) return true;
    return ((long)(el * 1000.0) / cfg->cursor_blink_ms) % 2 == 0;
}

double render_next_blink(const Config *cfg, double last_input, double now) {
    if (!cfg->cursor_blink || cfg->cursor_blink_ms <= 0) return -1.0;
    const double el = now - last_input;
    const double step = cfg->cursor_blink_ms / 1000.0;
    if (cfg->cursor_blink_timeout > 0 && el >= cfg->cursor_blink_timeout) return -1.0;

    double k = floor(el / step) + 1.0;
    double next = last_input + k * step;
    if (cfg->cursor_blink_timeout > 0 && next - last_input > cfg->cursor_blink_timeout)
        next = last_input + cfg->cursor_blink_timeout;      /* last wake-up: settle on "visible" */
    return next;
}

static void build_overlay(View *v, const Config *cfg, const Term *t, bool focused, bool blink_on) {
    QBuf *o = &v->ov;
    o->n = 0;

    VTermPos cp;
    vterm_state_get_cursorpos(t->state, &cp);
    const bool live = t->cursor_visible && t->sb_off == 0 &&
                      cp.row >= 0 && cp.row < t->rows && cp.col >= 0 && cp.col < t->cols;
    if (!live) return;

    const int cw = v->cw, ch = v->ch;
    const int x = cfg->padding + cp.col * cw;
    const int y = cfg->padding + cp.row * ch;
    const int th = cfg->cursor_thickness;
    const bool on = focused && blink_on;

    if (!focused) {                                   /* hollow box */
        qpush(o, solid(x, y, cw, 1, v->col_cursor));
        qpush(o, solid(x, y + ch - 1, cw, 1, v->col_cursor));
        qpush(o, solid(x, y, 1, ch, v->col_cursor));
        qpush(o, solid(x + cw - 1, y, 1, ch, v->col_cursor));
    } else if (on && v->cursor_style == 0) {          /* block: inverted cell */
        VTermScreenCell cell;
        if (!term_get_cell(t, cp.row, cp.col, &cell)) { memset(&cell, 0, sizeof cell); cell.width = 1; }
        if (cell.chars[0] == (uint32_t)-1) return;
        const int w = cell.width > 0 ? cell.width : 1;
        qpush(o, solid(x, y, w * cw, ch, v->col_cursor));
        emit_cell_glyphs(v, o, &cell, x, y, v->col_bg, cell.attrs.bold && cfg->fake_bold);
        if (cell.attrs.underline) qpush(o, solid(x, y + ch - 1, w * cw, 1, v->col_bg));
        if (cell.attrs.strike)    qpush(o, solid(x, y + ch / 2, w * cw, 1, v->col_bg));
    } else if (on && v->cursor_style == 1) {
        qpush(o, solid(x, y + ch - th, cw, th, v->col_cursor));
    } else if (on && v->cursor_style == 2) {
        qpush(o, solid(x, y, th, ch, v->col_cursor));
    }
}

/* -------------------------------------------------------------------- GL */

static const char *VS_SRC =
    "#version 330 core\n"
    "layout(location = 0) in ivec4 a_rect;\n"      /* x y w h (px) */
    "layout(location = 1) in uvec2 a_uv;\n"        /* atlas texel, 65535 = solid */
    "layout(location = 2) in vec4  a_col;\n"
    "uniform vec2 u_inv_view;\n"                   /* 2/width, 2/height */
    "flat out ivec2 v_base;\n"
    "flat out int   v_solid;\n"
    "out vec2 v_local;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    vec2 size = vec2(a_rect.zw);\n"
    "    vec2 p = vec2(a_rect.xy) + corner * size;\n"
    "    gl_Position = vec4(p.x * u_inv_view.x - 1.0, 1.0 - p.y * u_inv_view.y, 0.0, 1.0);\n"
    "    v_local = corner * size;\n"
    "    v_base  = ivec2(a_uv);\n"
    "    v_solid = (a_uv.x == 65535u) ? 1 : 0;\n"
    "    v_col   = a_col;\n"
    "}\n";

static const char *FS_SRC =
    "#version 330 core\n"
    "flat in ivec2 v_base;\n"
    "flat in int   v_solid;\n"
    "in vec2 v_local;\n"
    "in vec4 v_col;\n"
    "uniform sampler2D u_atlas;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "    float cov = (v_solid != 0) ? 1.0 : texelFetch(u_atlas, v_base + ivec2(v_local), 0).r;\n"
    "    float a = v_col.a * cov;\n"
    "    o = vec4(v_col.rgb * a, a);\n"                 /* premultiplied */
    "}\n";

static unsigned compile(GLenum type, const char *src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "silly-term: shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static unsigned make_program(void) {
    unsigned vs = compile(GL_VERTEX_SHADER, VS_SRC);
    unsigned fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "silly-term: link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static void setup_vao(unsigned vao, unsigned vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribIPointer(0, 4, GL_SHORT,          sizeof(Quad), (void *)offsetof(Quad, x));
    glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(Quad), (void *)offsetof(Quad, u));
    glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Quad), (void *)offsetof(Quad, c));
    for (unsigned i = 0; i < 3; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribDivisor(i, 1);
    }
}

static void vbo_upload(View *v, int i, const Quad *q, int n) {
    const size_t bytes = (size_t)n * sizeof(Quad);
    glBindBuffer(GL_ARRAY_BUFFER, v->vbo[i]);
    if (bytes > v->vbo_cap[i]) v->vbo_cap[i] = bytes + bytes / 2 + 4096;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)v->vbo_cap[i], NULL, GL_DYNAMIC_DRAW);   /* orphan */
    if (bytes) glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, q);
}

static void atlas_upload(View *v) {
    GlyphCache *g = &v->gc;
    glBindTexture(GL_TEXTURE_2D, v->tex);
    if (g->resized || v->tex_w != g->aw || v->tex_h != g->ah) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g->aw, g->ah, 0, GL_RED, GL_UNSIGNED_BYTE, g->pix);
        v->tex_w = g->aw;
        v->tex_h = g->ah;
        g->resized = false;
        int a, b;
        glyphs_take_dirty(g, &a, &b);
        return;
    }
    int y0, y1;
    if (glyphs_take_dirty(g, &y0, &y1))
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, g->aw, y1 - y0, GL_RED, GL_UNSIGNED_BYTE,
                        g->pix + (size_t)y0 * (size_t)g->aw);
}


/* ------------------------------------------------------- background image */

enum { BG_COVER, BG_CONTAIN, BG_STRETCH, BG_CENTER, BG_TILE };

static const char *BG_VS_SRC =
    "#version 330 core\n"
    "uniform vec4 u_rect;\n"                        /* x y w h, px */
    "uniform vec4 u_uv;\n"                          /* u0 v0 u1 v1 */
    "uniform vec2 u_inv_view;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    vec2 p = u_rect.xy + c * u_rect.zw;\n"
    "    gl_Position = vec4(p.x * u_inv_view.x - 1.0, 1.0 - p.y * u_inv_view.y, 0.0, 1.0);\n"
    "    v_uv = mix(u_uv.xy, u_uv.zw, c);\n"
    "}\n";

static const char *BG_FS_SRC =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_img;\n"
    "uniform float u_alpha;\n"
    "out vec4 o;\n"
    "void main() { o = texture(u_img, v_uv) * u_alpha; }\n";   /* texture is premultiplied */

static int parse_bg_mode(const char *m) {
    if (!*m || !strcmp(m, "cover"))   return BG_COVER;
    if (!strcmp(m, "contain"))        return BG_CONTAIN;
    if (!strcmp(m, "stretch") || !strcmp(m, "fill")) return BG_STRETCH;
    if (!strcmp(m, "center"))         return BG_CENTER;
    if (!strcmp(m, "tile"))           return BG_TILE;
    fprintf(stderr, "silly-term: unknown bg_image_mode '%s', using cover\n", m);
    return BG_COVER;
}

/* Optional: any failure leaves bg_tex == 0 and the plain colours are used. */
static void bg_init(View *v, const Config *cfg) {
    if (!cfg->bg_image[0]) return;

    int w, h;
    uint8_t *px = image_load(cfg->bg_image, &w, &h);
    if (!px) {
        fprintf(stderr, "silly-term: cannot load bg_image '%s', using bg colour\n", cfg->bg_image);
        return;
    }

    /* shrink (nearest) if the GPU cannot take it */
    int maxs = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxs);
    if (maxs > 0 && (w > maxs || h > maxs)) {
        int f = ((w > h ? w : h) + maxs - 1) / maxs;
        int nw = w / f, nh = h / f;
        uint8_t *sm = malloc((size_t)nw * (size_t)nh * 4);
        if (!sm) { free(px); return; }
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                memcpy(sm + ((size_t)y * nw + x) * 4, px + ((size_t)(y * f) * w + (size_t)(x * f)) * 4, 4);
        free(px);
        px = sm; w = nw; h = nh;
    }

    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {       /* premultiply */
        unsigned a = px[i * 4 + 3];
        if (a != 255) {
            px[i * 4 + 0] = (uint8_t)(px[i * 4 + 0] * a / 255);
            px[i * 4 + 1] = (uint8_t)(px[i * 4 + 1] * a / 255);
            px[i * 4 + 2] = (uint8_t)(px[i * 4 + 2] * a / 255);
        }
    }

    unsigned vs = compile(GL_VERTEX_SHADER, BG_VS_SRC);
    unsigned fs = compile(GL_FRAGMENT_SHADER, BG_FS_SRC);
    unsigned p = 0;
    if (vs && fs) {
        p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        int ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(p); p = 0; }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!p) { free(px); return; }

    v->bg_mode = parse_bg_mode(cfg->bg_image_mode);
    v->bg_prog = p;
    v->bg_w = w;
    v->bg_h = h;
    v->bg_u_rect  = glGetUniformLocation(p, "u_rect");
    v->bg_u_uv    = glGetUniformLocation(p, "u_uv");
    v->bg_u_inv   = glGetUniformLocation(p, "u_inv_view");
    v->bg_u_alpha = glGetUniformLocation(p, "u_alpha");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_img"), 1);          /* texture unit 1 */
    glGenVertexArrays(1, &v->bg_vao);

    const bool tile = v->bg_mode == BG_TILE;
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &v->bg_tex);
    glBindTexture(GL_TEXTURE_2D, v->bg_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, tile ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tile ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    free(px);
}

static void bg_draw(const View *v, const Config *cfg, int fbw, int fbh) {
    const float W = (float)fbw, H = (float)fbh, iw = (float)v->bg_w, ih = (float)v->bg_h;
    float rect[4] = { 0, 0, W, H };
    float uv[4]   = { 0, 0, 1, 1 };

    switch (v->bg_mode) {
    case BG_COVER: {                                  /* fill, crop the overflow */
        const float s = (W / iw > H / ih) ? W / iw : H / ih;
        const float fx = W / (iw * s), fy = H / (ih * s);        /* visible fraction */
        uv[0] = (1.0f - fx) / 2; uv[2] = uv[0] + fx;
        uv[1] = (1.0f - fy) / 2; uv[3] = uv[1] + fy;
        break;
    }
    case BG_CONTAIN: {                                /* fit inside, bars show the bg colour */
        const float s = (W / iw < H / ih) ? W / iw : H / ih;
        rect[2] = iw * s; rect[3] = ih * s;
        rect[0] = (W - rect[2]) / 2; rect[1] = (H - rect[3]) / 2;
        break;
    }
    case BG_CENTER:
        rect[2] = iw; rect[3] = ih;
        rect[0] = floorf((W - iw) / 2); rect[1] = floorf((H - ih) / 2);
        break;
    case BG_TILE:
        uv[2] = W / iw; uv[3] = H / ih;
        break;
    default: break;                                   /* BG_STRETCH */
    }

    glUseProgram(v->bg_prog);
    glUniform2f(v->bg_u_inv, 2.0f / W, 2.0f / H);
    glUniform4f(v->bg_u_rect, rect[0], rect[1], rect[2], rect[3]);
    glUniform4f(v->bg_u_uv, uv[0], uv[1], uv[2], uv[3]);
    glUniform1f(v->bg_u_alpha, cfg->bg_image_opacity * cfg->opacity);   /* opacity = background only */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, v->bg_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(v->bg_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* ------------------------------------------------------------------ view */

static int cursor_style_of(const Config *cfg) {
    return !strcmp(cfg->cursor_style, "underline") ? 1
         : !strcmp(cfg->cursor_style, "bar")       ? 2 : 0;
}

int view_init(View *v, const Config *cfg) {
    memset(v, 0, sizeof *v);
    v->fs = cfg->font_size;
    if (glyphs_init(&v->gc, cfg, v->fs) != 0) return -1;
    v->cw = v->gc.cw;
    v->ch = v->gc.ch;
    v->cursor_style = cursor_style_of(cfg);
    update_config_colors(v, cfg);

    v->prog = make_program();
    if (!v->prog) { glyphs_free(&v->gc); return -1; }
    v->u_inv_view = glGetUniformLocation(v->prog, "u_inv_view");
    glUseProgram(v->prog);
    glUniform1i(glGetUniformLocation(v->prog, "u_atlas"), 0);

    glGenVertexArrays(2, v->vao);
    glGenBuffers(2, v->vbo);
    for (int i = 0; i < 2; i++) setup_vao(v->vao[i], v->vbo[i]);

    glGenTextures(1, &v->tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);          /* premultiplied alpha */

    bg_init(v, cfg);

    v->all_dirty = true;
    v->main_stale = true;
    v->prev_off = -1;
    return 0;
}

void view_free(View *v) {
    if (v->prog) {
        glDeleteProgram(v->prog);
        glDeleteTextures(1, &v->tex);
        glDeleteBuffers(2, v->vbo);
        glDeleteVertexArrays(2, v->vao);
    }
    if (v->bg_tex) {
        glDeleteTextures(1, &v->bg_tex);
        glDeleteProgram(v->bg_prog);
        glDeleteVertexArrays(1, &v->bg_vao);
    }
    rows_free(v);
    free(v->ov.q);
    free(v->main_buf);
    free(v->row_buf);
    glyphs_free(&v->gc);
    memset(v, 0, sizeof *v);
}

void view_invalidate(View *v) { v->all_dirty = true; }

int view_set_font_size(View *v, const Config *cfg, int size) {
    if (size < 6)   size = 6;
    if (size > 200) size = 200;
    if (size == v->fs) return 0;

    if (glyphs_set_size(&v->gc, cfg, size) != 0) return 0;     /* failure keeps the old font */
    v->fs = size;
    v->cw = v->gc.cw;
    v->ch = v->gc.ch;
    v->all_dirty = true;
    return 1;
}

int view_pixel_width(const View *v, const Config *cfg, int cols)  { return cols * v->cw + 2 * cfg->padding; }
int view_pixel_height(const View *v, const Config *cfg, int rows) { return rows * v->ch + 2 * cfg->padding; }

/* ------------------------------------------------------------------ frame */

static void sel_rows(const Selection *s, int *lo, int *hi) {
    *lo = s->r0 < s->r1 ? s->r0 : s->r1;
    *hi = s->r0 < s->r1 ? s->r1 : s->r0;
}

static bool sel_same(const Selection *a, const Selection *b) {
    if (a->active != b->active) return false;
    return !a->active || (a->r0 == b->r0 && a->c0 == b->c0 && a->r1 == b->r1 && a->c1 == b->c1);
}

void render_frame(View *v, const Config *cfg, Term *t, const Selection *sel,
                  double last_input, bool focused, int fbw, int fbh, double now) {
    static const Selection no_sel = { 0 };
    if (!sel) sel = &no_sel;

    if (!v->palette_ready || v->palette_screen != t->screen) update_vterm_palette(v, t);

    if (t->rows != v->nrows && !rows_resize(v, t->rows)) return;
    if (v->row_cap < t->cols) {
        VTermScreenCell *nb = realloc(v->row_buf, (size_t)t->cols * sizeof *nb);
        if (!nb) return;
        v->row_buf = nb;
        v->row_cap = t->cols;
    }

    /* ---- which rows need rebuilding? */
    bool all = v->all_dirty || t->sb_off != v->prev_off || (t->sb_off > 0 && term_has_damage(t));
#ifdef SILLY_FULL_REBUILD            /* debug: rebuild every row every frame (to diff against the cache) */
    all = true;
#endif
    int lo1 = 1, hi1 = 0, lo2 = 1, hi2 = 0;                       /* empty ranges */
    if (!sel_same(sel, &v->prev_sel)) {
        if (v->prev_sel.active) sel_rows(&v->prev_sel, &lo1, &hi1);
        if (sel->active)        sel_rows(sel, &lo2, &hi2);
    }

    long sa = 0, sb = -1;
    const bool has_sel = sel_range(sel, t, &sa, &sb);

    bool rebuilt = false;
    for (int pass = 0; pass < 2; pass++) {
        const unsigned epoch = v->gc.epoch;
        for (int r = 0; r < t->rows; r++) {
            if (!(all || term_row_damaged(t, r) || (r >= lo1 && r <= hi1) || (r >= lo2 && r <= hi2)))
                continue;
            build_row(v, cfg, t, has_sel, sa, sb, r);
            rebuilt = true;
        }
        if (v->gc.epoch == epoch) break;                          /* atlas was cleared mid-build: redo */
        all = true;
    }
    v->all_dirty  = false;
    v->prev_off   = t->sb_off;
    v->prev_sel   = *sel;
    term_clear_damage(t);

    /* ---- grid VBO (only when something changed) */
    if (rebuilt || v->main_stale) {
        size_t total = 0;
        for (int r = 0; r < v->nrows; r++) total += (size_t)(v->rows[r].bg.n + v->rows[r].fg.n);
        if (total > v->main_cap) {
            Quad *nb = realloc(v->main_buf, total * sizeof *nb);
            if (!nb) return;
            v->main_buf = nb;
            v->main_cap = total;
        }
        size_t off = 0;
        for (int r = 0; r < v->nrows; r++) {
            if (v->rows[r].bg.n)
                memcpy(v->main_buf + off, v->rows[r].bg.q, (size_t)v->rows[r].bg.n * sizeof(Quad));
            off += (size_t)v->rows[r].bg.n;
        }
        for (int r = 0; r < v->nrows; r++) {
            if (v->rows[r].fg.n)
                memcpy(v->main_buf + off, v->rows[r].fg.q, (size_t)v->rows[r].fg.n * sizeof(Quad));
            off += (size_t)v->rows[r].fg.n;
        }
        v->main_n = (int)total;
        v->main_stale = false;
        vbo_upload(v, 0, v->main_buf, v->main_n);
    }

    /* ---- cursor VBO (tiny, every frame) */
    build_overlay(v, cfg, t, focused, render_blink_on(cfg, last_input, now));
    vbo_upload(v, 1, v->ov.q, v->ov.n);

    atlas_upload(v);
    v->atlas_epoch = v->gc.epoch;

    /* ---- draw */
    glViewport(0, 0, fbw, fbh);
    const float o = cfg->opacity;
    glClearColor((float)v->col_bg.r / 255.0f * o, (float)v->col_bg.g / 255.0f * o,
                 (float)v->col_bg.b / 255.0f * o, o);
    glClear(GL_COLOR_BUFFER_BIT);

    if (v->bg_tex) bg_draw(v, cfg, fbw, fbh);         /* under the grid; default-bg cells let it show */

    glUseProgram(v->prog);
    glUniform2f(v->u_inv_view, 2.0f / (float)fbw, 2.0f / (float)fbh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->tex);

    if (v->main_n > 0) {
        glBindVertexArray(v->vao[0]);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, v->main_n);
    }
    if (v->ov.n > 0) {
        glBindVertexArray(v->vao[1]);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, v->ov.n);
    }
}
