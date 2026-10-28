# `load_conf.c` / `load_conf.h` — Maintainer & Extension Guide

**Purpose:** keep the config layer trivial to extend and to strip down. One flat `Opt` table drives everything; the header is the single source of truth for what is configurable.

**Philosophy (inherited from the main file):**
- The `OPTS[]` table *is* the schema. Adding a config key = one row. Removing one = delete one row + the field.
- No per-type parser functions beyond what already exists (`parse_color`, `parse_bool`). Numeric parsing is inline.
- Every field in `Config` must appear exactly once in `OPTS[]` (or be explicitly marked "not user-configurable" in a comment). Otherwise it's dead.
- Comments in `load_conf.h` explain *units and semantics*, not *what the type is*. Keep that discipline.

---

## 1. Code map

| Region | Responsibility | Change here when… |
|---|---|---|
| `load_conf.h` — `Rgb` | Tiny color struct shared with the renderer. | Never. It's the ABI between config and render. |
| `load_conf.h` — `Config` | The schema the rest of the program reads. | Adding/removing/retyping a config value. |
| `load_conf.c` — `Opt` + macros | Type-tagged offset table. | Only if a *new type* is needed (rare). |
| `load_conf.c` — `OPTS[]` | The actual config surface. | Any key add/remove/range change. |
| `set_defaults` | Guarantees every field is initialized. | Any new field. |
| `default_conf_path` | XDG/HOME resolution. | Never. |
| `trim`, `parse_color`, `parse_bool` | Small helpers. | Only if used ≥2 places. |
| `load_conf` | The parse loop. | Only for syntax changes (new comment style, new quoting, new value form). |

**Rule:** the file is a *data-driven parser*. Resist turning it into an object-oriented config system. If a change adds a `switch` that could be a table row, it's wrong.

---

## 2. The `Opt` contract

```c
typedef struct {
    const char *key;   /* config file key */
    int         type;  /* T_STR | T_INT | T_FLOAT | T_BOOL | T_COLOR */
    size_t      off;   /* offsetof(Config, field) */
    size_t      size;  /* only used by T_STR (buffer capacity) */
    double      lo, hi;/* only used by T_INT / T_FLOAT (clamped range) */
} Opt;
```

Invariants, enforced by `OPTS[]` construction:

1. `off` is always `offsetof(Config, field)` — never hand-written.
2. `size` is always `sizeof(((Config *)0)->field)` for strings — never hand-written.
3. `lo <= hi` for numeric ranges; strings, bools, colors ignore both.
4. Keys are unique. `load_conf` does a linear scan; a duplicate key silently shadows — don't allow it. (See §4.3 for a cheap assertion.)
5. The `Config` field pointed to by `off` must match `type`. The macros `O_INT`, `O_STR`, etc. don't enforce this — the compiler can't either. It's a review-time invariant.

**Do not** add a type unless three or more fields need it. Existing five cover everything in `Config` today.

---

## 3. Extension recipes

### 3.1 Add a simple key (the common case)

Example: `blink_speed` — number of blinks per second, 1–10, int.

1. In `load_conf.h`, add to `Config` under the right comment block:
   ```c
   int blink_speed;          /* blinks per second, 1-10 */
   ```
2. In `load_conf.c` `OPTS[]`, add one row in the matching group:
   ```c
   O_INT("blink_speed", blink_speed, 1, 10),
   ```
3. In `set_defaults`, set a value:
   ```c
   c->blink_speed = 2;
   ```
4. Done. `load_conf` picks it up automatically.

**Line cost:** 3 lines. Anything more means you added logic where a table row would do.

### 3.2 Add a string key

Same as §3.1 with `O_STR("font_path_alt", font_path_alt)` and a `char font_path_alt[256]` field. `set_defaults` should `snprintf(c->font_path_alt, sizeof c->font_path_alt, "")` — empty means "unset", which is the convention used for `font_path`.

### 3.3 Add a color key

Same as §3.1 with `O_COL("cursor_text", cursor_text)` and `Rgb cursor_text` in `Config`. `set_defaults` must set it to a concrete `hex(...)` — **there is no "unset" state for colors** in the current design. If you need one, prefer a separate `bool has_cursor_text` over a sentinel color.

### 3.4 Add a new type (rare — justify it)

Only if the value can't be expressed as one of the five. Example: a duration like `"500ms"` or `"2s"`.

1. Add `T_DURATION` to the `enum`.
2. Add `O_DUR(k, f, lo_ms, hi_ms)` macro.
3. Add a `case T_DURATION` in `load_conf`'s `switch` with a single `parse_duration(val, &ms)` helper.
4. Add `parse_duration` next to `parse_bool`.

**Reject this** if the value could just be an int in milliseconds. `cursor_blink_ms` is already an int — follow that precedent.

### 3.5 Add a new bind key

The binds are already a flat block at the end of `OPTS[]`. To add "toggle fullscreen":

1. `char key_fullscreen[48];` in `Config` under the keybind comment.
2. `O_STR("key_fullscreen", key_fullscreen)` in `OPTS[]` next to the others.
3. `snprintf(c->key_fullscreen, sizeof c->key_fullscreen, "f11")` in `set_defaults`.
4. Wire it in `main.c` (see the main guide §3.1).

No changes to the parser itself.

### 3.6 Change a clamp range

Edit the `lo`/`hi` in the `OPTS[]` row. Nothing else. Note that `set_defaults` is *not* clamped — if you tighten a range below the current default, update the default too, or `load_conf` will silently clamp on first read of a file that doesn't set the key.

**Invariant:** `set_defaults` must produce values inside every `lo..hi` range in `OPTS[]`. Add a comment on any default that sits at a range edge.

### 3.7 Remove a key

1. Delete the field from `Config`.
2. Delete the row from `OPTS[]`.
3. Delete the assignment in `set_defaults`.
4. Grep `cfg.<field>` across the project — fix every read.

If old config files still set it, `load_conf` will print `unknown key` to stderr. That's acceptable — it's a warning, not an error, and it's the correct signal to the user. **Do not** add a deprecation table. That's over-engineering for a program this size.

---

## 4. Simplification checklist

Run before every PR touching this pair of files.

### 4.1 Dead fields
- [ ] Every `Config` field appears in `OPTS[]` — or has an explicit `/* not user-configurable */` comment explaining why.
- [ ] Every `Config` field is written in `set_defaults`.
- [ ] Every `Config` field is read at least once outside `load_conf.{c,h}`.
- [ ] Every `OPTS[]` row's `off` is `offsetof(Config, …)` — grep for `offsetof` and confirm all rows use a macro.

Any failure = delete the field or delete the row.

### 4.2 Dead helpers
- [ ] `trim`, `parse_color`, `parse_bool`, `hex` — each used at least once. If a helper has one caller, inline it.
- [ ] No unused `case` in the `switch`.

### 4.3 Table sanity (cheap to add, high value)

If the table grows past ~50 rows, add a one-time startup check in debug builds:

```c
#ifndef NDEBUG
static void assert_opts_unique(void) {
    for (size_t i = 0; i < sizeof OPTS / sizeof *OPTS; i++)
        for (size_t j = i + 1; j < sizeof OPTS / sizeof *OPTS; j++)
            if (!strcmp(OPTS[i].key, OPTS[j].key))
                fprintf(stderr, "load_conf: duplicate key '%s'\n", OPTS[i].key);
}
#endif
```

Call it once from `load_conf`. Do **not** add it to release builds — startup time matters and the table is static.

### 4.4 Over-engineering smells (reject in review)

- A `Config` section struct (`struct { int size; int spacing; } font;`) — flattens the `OPTS[]` offsets into nested `offsetof`, breaks the `O_*` macros, saves nothing. **Rejected.**
- A `register_opt()` API — the table is `static const`; that's the whole point.
- A config validation callback per key — `lo`/`hi` is enough for numbers; strings are trusted; colors are range-checked by `parse_color`.
- A `has_<field>` boolean for every string — only `font_path` needs one, and it uses `*c->font_path == 0` instead. Follow that pattern.
- An `unknown_keys` list collected for post-parse reporting — printing to stderr immediately is fine.
- Comments restating types (`int font_size;  /* font size */`) — the field name is the doc; add units/semantics only.

### 4.5 Header comment discipline

`load_conf.h` is documentation. Keep:
- Field comments that state *units* (`/* multiplier of font_size */`, `/* empty = try common system fonts */`, `/* command line, split on spaces */`).
- The keybind example in the `/* keybinds */` comment — it doubles as the parser's grammar spec.

Delete:
- `/* the font size */` above `int font_size;`.
- `/* window title */` above `char title[128];`.
- Anything that reads back the type.

---

## 5. Worked patches

### 5.1 Add `scroll_page_lines` (net +3 lines)

```c
/* load_conf.h, under /* scrolling */
int scroll_page_lines;    /* lines per PageUp/PageDown, default 20 */

/* load_conf.c, OPTS[] */
O_INT("scroll_page_lines", scroll_page_lines, 1, 500),

/* set_defaults */
c->scroll_page_lines = 20;
```

Main file reads `cfg.scroll_page_lines` instead of `T.rows / 2` in `handle_keyboard`. The default preserves current behavior when unset.

### 5.2 Remove `letter_spacing` (net −5 lines, and simplifies `main`)

```c
/* load_conf.h — delete */
int letter_spacing;

/* load_conf.c — delete from OPTS[] */
O_INT("letter_spacing", letter_spacing, -10, 50),

/* load_conf.c — delete from set_defaults */
/* (there is no explicit assignment — memset zeroes it; nothing to delete here) */

/* main.c — replace */
g_cw = (int)ceilf(m.x) + cfg.letter_spacing;
/* with */
g_cw = (int)ceilf(m.x);
```

This is a good example: the default was `0`, the parser had a branch for it, and it existed only to nudge cell width. Removing it eliminates a config surface, a `lo/hi` range, and a `+` in hot code. Net win.

### 5.3 Collapse `hex` into callers

`hex` has exactly one caller (`parse_color`) and one caller in `set_defaults` (16 times, via `pal[i]`). Inline into `set_defaults`? No — 16 inlines is worse than one call. Inline into `parse_color`? Marginal.

**Verdict:** leave `hex`. It's used ≥2 conceptual places. This is exactly the kind of function the "used ≥2 places" rule protects.

---

## 6. Contributor checklist

Before opening a PR touching `load_conf.{c,h}`:

- [ ] Every new `Config` field has: a comment with units/semantics, an `OPTS[]` row, and a `set_defaults` assignment.
- [ ] Every `OPTS[]` row uses the `O_*` macro — no hand-written `offsetof`.
- [ ] `set_defaults` output is inside every `lo..hi` range.
- [ ] Key names are `snake_case`, match the field name where possible, and don't collide.
- [ ] Numeric ranges clamp sensibly (no `hi == INT_MAX` to mean "unbounded" — pick a real ceiling).
- [ ] String fields use `char[N]` with an explicit capacity and `snprintf(c->field, sizeof c->field, …)` in `set_defaults`.
- [ ] Colors default to a concrete `hex(...)`, never zeroed (zero means black, which is a valid color, not "unset").
- [ ] No new helper unless it has ≥2 callers.
- [ ] No new `switch` case unless a new type exists.
- [ ] Header comment changes follow §4.5 (units yes, type restatement no).

If a checkbox fails, fix before review.

---

## 7. Where NOT to touch

Small, load-bearing, and correct as-is:

- `default_conf_path` — XDG-first, HOME-fallback, `-1` on failure. Don't add Windows/`%APPDATA%` handling unless someone ports the main binary; it's dead code today.
- The inline-comment rule (`whitespace + '#'`) — deliberately allows `#rrggbb` values. Don't "simplify" it to `strchr(val, '#')`; that breaks every color in the file.
- Quoted-string stripping — handles both `"…"` and `'…'` and requires the quote pair to match. Don't extend it to escape sequences unless a user asks.
- `T_STR` size source — `o->size` comes from `sizeof(((Config *)0)->f)` in the macro. Never change this to a hand-written size; it's the only thing preventing truncation bugs when a field is resized.
- `load_conf` returning `0` even with per-line errors — this is intentional. Missing file = `-1`; malformed file = still usable with defaults. Both `main` and users depend on this.
- `set_defaults` calling `memset` first — guarantees no uninitialized bytes leak into the config or into a `Config` copy. Don't remove.

---

## 8. Line budget

| Region | ~lines |
|---|---|
| `load_conf.h` | 65 |
| `Opt` + macros | 20 |
| `OPTS[]` | 55 |
| `hex` + `set_defaults` | 60 |
| `default_conf_path` | 15 |
| helpers (`trim`, `parse_color`, `parse_bool`) | 35 |
| `load_conf` | 75 |
| **total** | **~325** |

The `OPTS[]` table should grow at the same rate as `Config` — one row per field, roughly. If `OPTS[]` grows faster than `Config`, you've added keys that don't map to fields; if slower, you've added fields the user can't configure (fine, but justify in the PR).

The parser body (`load_conf`) should stay under ~80 lines. If it grows, you're adding syntax features — question whether the feature is needed before touching it.

If `parse_color` + `parse_bool` + `trim` exceed ~40 lines combined, split into helpers *only if* the split removes shared lines. Otherwise leave duplicated-looking code as-is; small helpers are not the enemy of simplicity.