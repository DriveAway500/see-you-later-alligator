#include "headers/glyph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ATLAS_W      1024
#define ATLAS_H_INIT 256
#define ATLAS_H_MAX  4096

/* ------------------------------------------------------------------ fonts */

static const char *const FONT_CANDIDATES[] = {
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansMono-Regular.ttf",
    /* symbol / box / braille coverage for fallback */
    "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
    /* CJK */
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
};

static void add_path(GlyphCache *g, const char *p) {
    if (!p || !*p || g->nfaces >= GLYPH_MAX_FACES) return;
    for (int i = 0; i < g->nfaces; i++)
        if (!strcmp(g->paths[i], p)) return;
    if (access(p, R_OK) != 0) return;
    g->paths[g->nfaces] = strdup(p);
    if (g->paths[g->nfaces]) g->nfaces++;
}

/* Last resort for unusual distros: ask fontconfig once. */
static void add_fontconfig_default(GlyphCache *g) {
    FILE *f = popen("fc-match -f '%{file}' monospace 2>/dev/null", "r");
    if (!f) return;
    char buf[512];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    pclose(f);
    buf[n] = 0;
    add_path(g, buf);
}

static bool open_face(GlyphCache *g, int i) {
    if (g->faces[i]) return true;
    if (g->tried[i]) return false;
    g->tried[i] = true;
    FT_Face face;
    if (FT_New_Face(g->ft, g->paths[i], 0, &face)) return false;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) || FT_Set_Pixel_Sizes(face, 0, (FT_UInt)g->px)) {
        FT_Done_Face(face);
        return false;
    }
    g->faces[i] = face;
    return true;
}

/* ------------------------------------------------------------------ atlas */

static void atlas_clear(GlyphCache *g) {
    memset(g->pix, 0, (size_t)g->aw * (size_t)g->ah);
    g->pen_x = g->pen_y = g->row_h = 0;
    g->dirty_y0 = g->dirty_y1 = 0;
    memset(g->tab, 0, g->tab_cap * sizeof *g->tab);
    g->tab_n = 0;
    g->epoch++;
}

static bool atlas_grow(GlyphCache *g) {
    if (g->ah >= ATLAS_H_MAX) return false;
    int nh = g->ah * 2;
    uint8_t *np = realloc(g->pix, (size_t)g->aw * (size_t)nh);
    if (!np) return false;
    memset(np + (size_t)g->aw * (size_t)g->ah, 0, (size_t)g->aw * (size_t)(nh - g->ah));
    g->pix = np;
    g->ah = nh;
    g->resized = true;
    return true;
}

/* Reserve w*h texels. May grow or (as a last resort) clear the atlas. */
static bool atlas_alloc(GlyphCache *g, int w, int h, int *x, int *y) {
    if (w > g->aw || h > ATLAS_H_MAX) return false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (g->pen_x + w > g->aw) {            /* next shelf */
            g->pen_y += g->row_h;
            g->pen_x = 0;
            g->row_h = 0;
        }
        if (g->pen_y + h <= g->ah) {
            *x = g->pen_x;
            *y = g->pen_y;
            g->pen_x += w;
            if (h > g->row_h) g->row_h = h;
            return true;
        }
        if (!atlas_grow(g)) atlas_clear(g);
    }
    return false;
}

static void dirty_rows(GlyphCache *g, int y0, int y1) {
    if (g->dirty_y0 >= g->dirty_y1) { g->dirty_y0 = y0; g->dirty_y1 = y1; return; }
    if (y0 < g->dirty_y0) g->dirty_y0 = y0;
    if (y1 > g->dirty_y1) g->dirty_y1 = y1;
}

bool glyphs_take_dirty(GlyphCache *g, int *y0, int *y1) {
    if (g->dirty_y0 >= g->dirty_y1) return false;
    *y0 = g->dirty_y0;
    *y1 = g->dirty_y1;
    g->dirty_y0 = g->dirty_y1 = 0;
    return true;
}

/* ------------------------------------------------------------- hash table */

static size_t hash_cp(uint32_t cp) { return (size_t)(cp * 2654435761u); }

static GlyphSlot *tab_find(GlyphCache *g, uint32_t cp) {
    size_t mask = g->tab_cap - 1;
    for (size_t i = hash_cp(cp) & mask;; i = (i + 1) & mask)
        if (g->tab[i].cp == cp || g->tab[i].cp == 0) return &g->tab[i];
}

static bool tab_grow(GlyphCache *g) {
    size_t ncap = g->tab_cap ? g->tab_cap * 2 : 512;
    GlyphSlot *old = g->tab;
    size_t ocap = g->tab_cap;
    GlyphSlot *nt = calloc(ncap, sizeof *nt);
    if (!nt) return false;
    g->tab = nt;
    g->tab_cap = ncap;
    for (size_t i = 0; i < ocap; i++)
        if (old[i].cp) *tab_find(g, old[i].cp) = old[i];
    free(old);
    return true;
}

static void tab_put(GlyphCache *g, uint32_t cp, Glyph gl) {
    if ((g->tab_n + 1) * 10 > g->tab_cap * 7 && !tab_grow(g)) return;
    GlyphSlot *s = tab_find(g, cp);
    if (!s->cp) g->tab_n++;
    s->cp = cp;
    s->g = gl;
}

/* --------------------------------------------------------------- raster */

static bool is_blank_cp(uint32_t cp) {
    return cp <= 0x20 || (cp >= 0x7F && cp <= 0xA0) || cp == 0xAD ||
           (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x2064) || cp == 0xFEFF;
}

/* Returns 1 = rasterised (possibly empty), 0 = no font has the glyph. */
static int raster(GlyphCache *g, uint32_t cp, Glyph *out) {
    for (int i = 0; i < g->nfaces; i++) {
        if (!open_face(g, i)) continue;
        FT_Face face = g->faces[i];
        FT_UInt idx = FT_Get_Char_Index(face, cp);
        if (!idx) continue;

        FT_Int32 flags = FT_LOAD_RENDER | FT_LOAD_NO_BITMAP |
                         (g->smooth ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
        if (FT_Load_Glyph(face, idx, flags)) continue;

        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap *bm = &slot->bitmap;
        memset(out, 0, sizeof *out);
        if (bm->width == 0 || bm->rows == 0) return 1;      /* e.g. a blank glyph */

        int x, y;
        if (!atlas_alloc(g, (int)bm->width, (int)bm->rows, &x, &y)) return 1;

        for (unsigned r = 0; r < bm->rows; r++) {
            uint8_t *dst = g->pix + (size_t)(y + (int)r) * (size_t)g->aw + (size_t)x;
            const uint8_t *src = bm->buffer + (ptrdiff_t)r * bm->pitch;
            switch (bm->pixel_mode) {
            case FT_PIXEL_MODE_MONO:
                for (unsigned c = 0; c < bm->width; c++)
                    dst[c] = (src[c >> 3] & (0x80 >> (c & 7))) ? 255 : 0;
                break;
            case FT_PIXEL_MODE_BGRA:                         /* colour font: keep the alpha */
                for (unsigned c = 0; c < bm->width; c++) dst[c] = src[c * 4 + 3];
                break;
            default:                                         /* FT_PIXEL_MODE_GRAY */
                memcpy(dst, src, bm->width);
            }
        }
        dirty_rows(g, y, y + (int)bm->rows);

        out->x = (int16_t)slot->bitmap_left;
        out->y = (int16_t)(g->baseline - slot->bitmap_top);
        out->w = (uint16_t)bm->width;
        out->h = (uint16_t)bm->rows;
        out->u = (uint16_t)x;
        out->v = (uint16_t)y;
        return 1;
    }
    return 0;
}

Glyph glyphs_get(GlyphCache *g, uint32_t cp) {
    static const Glyph none = { 0 };
    if (is_blank_cp(cp)) return none;

    if (g->tab_cap) {
        GlyphSlot *s = tab_find(g, cp);
        if (s->cp == cp) return s->g;
    }

    Glyph gl = none;
    if (!raster(g, cp, &gl) && !raster(g, 0xFFFD, &gl) && !raster(g, '?', &gl))
        gl = none;
    tab_put(g, cp, gl);
    return gl;
}

/* --------------------------------------------------------------- metrics */

static int measure(GlyphCache *g, const Config *cfg) {
    if (!open_face(g, 0)) return -1;
    FT_Face f = g->faces[0];

    if (FT_Load_Char(f, 'M', FT_LOAD_DEFAULT)) return -1;
    int adv = (int)ceil((double)f->glyph->advance.x / 64.0);
    g->cw = adv + cfg->letter_spacing;
    g->ch = (int)ceil((double)g->px * (double)cfg->line_height);
    if (g->cw < 1) g->cw = 1;
    if (g->ch < 1) g->ch = 1;

    int asc  = (int)ceil((double)f->size->metrics.ascender / 64.0);
    int desc = (int)ceil((double)-f->size->metrics.descender / 64.0);
    g->baseline = (g->ch - (asc + desc)) / 2 + asc;
    return 0;
}

int glyphs_init(GlyphCache *g, const Config *cfg, int px) {
    memset(g, 0, sizeof *g);
    g->px     = px;
    g->smooth = cfg->font_smooth;
    if (FT_Init_FreeType(&g->ft)) return -1;

    add_path(g, cfg->font_path);
    for (size_t i = 0; i < sizeof FONT_CANDIDATES / sizeof *FONT_CANDIDATES; i++)
        add_path(g, FONT_CANDIDATES[i]);

    /* Primary = first path that actually opens (kept in slot 0). */
    for (bool fc = false;;) {
        while (g->nfaces > 0 && !open_face(g, 0)) {
            free(g->paths[0]);
            memmove(&g->paths[0], &g->paths[1], (size_t)(g->nfaces - 1) * sizeof g->paths[0]);
            memmove(&g->tried[0], &g->tried[1], (size_t)(g->nfaces - 1) * sizeof g->tried[0]);
            g->nfaces--;
            g->tried[g->nfaces] = false;
        }
        if (g->nfaces > 0) break;
        if (fc) {
            fprintf(stderr, "silly-term: no usable font found (set font_path)\n");
            glyphs_free(g);
            return -1;
        }
        add_fontconfig_default(g);
        fc = true;
    }

    if (measure(g, cfg) != 0) { glyphs_free(g); return -1; }

    g->aw  = ATLAS_W;
    g->ah  = ATLAS_H_INIT;
    g->pix = calloc((size_t)g->aw * (size_t)g->ah, 1);
    if (!g->pix || !tab_grow(g)) { glyphs_free(g); return -1; }
    g->resized = true;
    return 0;
}

int glyphs_set_size(GlyphCache *g, const Config *cfg, int px) {
    if (px == g->px) return 0;
    int old = g->px;
    g->px = px;
    for (int i = 0; i < g->nfaces; i++)
        if (g->faces[i] && FT_Set_Pixel_Sizes(g->faces[i], 0, (FT_UInt)px)) {
            if (i == 0) {                          /* primary refuses: roll back */
                g->px = old;
                for (int j = 0; j < i; j++)
                    if (g->faces[j]) FT_Set_Pixel_Sizes(g->faces[j], 0, (FT_UInt)old);
                return -1;
            }
        }
    if (measure(g, cfg) != 0) return -1;
    atlas_clear(g);
    return 0;
}

void glyphs_free(GlyphCache *g) {
    for (int i = 0; i < g->nfaces; i++) {
        if (g->faces[i]) FT_Done_Face(g->faces[i]);
        free(g->paths[i]);
    }
    if (g->ft) FT_Done_FreeType(g->ft);
    free(g->pix);
    free(g->tab);
    memset(g, 0, sizeof *g);
}
