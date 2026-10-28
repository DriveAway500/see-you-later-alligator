# silly-term — Maintainer & Extension Guide

**Purpose:** keep the codebase easy for other devs to modify, and aggressively reduce dead/unnecessary lines to preserve maximum simplicity.

**Philosophy (read this first):**
- One file, flat state, no abstraction layers unless they remove *more* lines than they add.
- Every struct field, every function, every config key must have a live caller. If it doesn't, delete it.
- Prefer explicit code over generic machinery. A 3-line `if` beats a callback table used once.
- New features must justify their line cost. "It might be useful later" = rejected.

---

## 1. Code map

| Region | Lines | Responsibility | Change here when… |
|---|---|---|---|
| `state` | T, S, globals | All runtime state lives in `T` (terminal) and `S` (selection). | Adding persistent state — extend `T`/`S`, never add new file-scope globals. |
| `util` | `C`, `utf8_enc`, `write_all` | Small helpers. | Only if a new helper is used ≥2 places. |
| `keybinds` | `key_by_name`, `parse_bind`, `pressed`, `bind_hit` | Config string → `Bind` → hit-test. | Adding a new bindable action. |
| `vterm callbacks` | `out_cb`, `cb_*`, `SCREEN_CB` | libvterm → PTY and → scrollback/props. | Handling new `VTermProp` / screen events. |
| `pty` | `spawn_shell`, `pump`, `apply_size` | Process + non-blocking read loop. | Changing shell spawn or resize policy. |
| `cell access` | `get_cell`, `resolve` | Unified viewport+scrollback cell fetch and color resolution. | Any new rendering/selection feature reads through here. |
| `selection` | `sel_range`, `in_sel`, `copy_selection`, `paste` | Mouse selection and clipboard. | New selection mode (block, word). |
| `input` | `typed`, `scroll_by`, `handle_keyboard`, `handle_mouse` | All user input → vterm. | New key/mouse behavior. |
| `render` | `render` | Draw cells, cursor, decorations. | New visual attribute. |
| `font` | `load_font` | Font discovery + glyph ranges. | Adding glyph coverage. |
| `main` | `main` | Config load, window, loop, cleanup. | Startup/shutdown semantics. |

**Rule:** before adding a function, check whether it belongs to an existing region. If yes, put it there. If it needs a new region, it needs a *strong* reason.

---

## 2. The state contract

All mutable state is in three places:

- `T` — terminal: vterm objects, PTY fd/pid, dimensions, scrollback ring, cursor/title/altscreen flags.
- `S` — selection: `active`, `dragging`, anchor `(r0,c0)`, cursor `(r1,c1)`.
- File-scope scalars — `g_font`, `g_cw`, `g_ch`, `g_fs`, `g_cursor_style`, `g_last_input`, and the four `Bind`s.

**To add state:** put it in `T` or `S`. Do **not** add a new file-scope global unless it is truly singleton config-derived (like `g_font`). If you're tempted to add `static int g_foo;`, ask whether it belongs in `T` first.

**To remove state:** grep the field name across the file. If it has one write and zero reads (or vice versa), delete it.

---

## 3. Extension recipes

Each recipe shows the *minimal* patch. Resist adding scaffolding.

### 3.1 Add a config-driven keybind

1. Add the field to `Config` in `load_conf.h` + parse it in `load_conf.c`.
2. Add a `Bind` beside `B_copy`:
   ```c
   static Bind B_new;
   ```
3. Init in `main`:
   ```c
   B_new = parse_bind(cfg.key_new);
   ```
4. Handle it in `handle_keyboard` — add **one** `else if` branch:
   ```c
   else if (bind_hit(&B_new, ctrl, shift, alt)) { do_new_thing(); bound = true; }
   ```
5. Implement `do_new_thing()` next to the other action helpers (e.g. near `copy_selection`).

**Line cost:** ~5 lines. Anything more means you're over-engineering.

### 3.2 Add a rendering attribute (e.g. italic, dim)

1. In `render()`, inside the cell loop, after `bold`:
   ```c
   bool italic = cell.attrs.italic;
   ```
2. Pass it to `resolve()` only if it changes color (dim → darker fg). Otherwise just use it in the draw call:
   ```c
   if (italic) { /* if the font has no italic, skip or fake-slant */ }
   ```
3. If you need config to toggle it, add `cfg.fake_italic` — but only if there's a real use case.

**Do not** add a `CellStyle` struct or a style resolver. Inline is fine.

### 3.3 Add a new `VTermProp` handler

In `cb_prop`, add a `case`:
```c
case VTERM_PROP_CURSORBLINK:
    T.cursor_blink = val->boolean;
    break;
```
Then read `T.cursor_blink` in `render`. One field, one case, one read.

### 3.4 Add a scrollback feature (e.g. search)

The scrollback lives in `T.sb` as a ring buffer indexed by `sb_line(i)`. Any feature that walks history should:

1. Iterate `i` from `0` to `T.sb_count - 1`.
2. Use `sb_line(i)` to get the `SbLine*`.
3. Read `l->cells[col]` directly — same `VTermScreenCell` layout as the viewport.

Do **not** create a `ScrollbackIterator` abstraction. A `for` loop over `sb_line` is the interface.

### 3.5 Change the PTY pump policy

`pump()` reads up to 512 KiB per frame. To change the budget or add throttling, edit the `(1u << 19)` constant and the `EAGAIN` handling. There is no config knob for this on purpose — add one only if users report a real problem.

### 3.6 Add a new mouse action

In `handle_mouse`, after the existing button checks, add:
```c
if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { do_right_click(); }
```
Keep it a flat `if`. The mouse handler is intentionally a straight-line list of button events.

---

## 4. Simplification checklist (the "reduce lines" mandate)

Run this before every PR. Every item you can check is lines removed.

### 4.1 Dead code
- [ ] Grep every `static` function name. Zero callers → delete.
- [ ] Grep every `T.` / `S.` field. Zero reads or zero writes → delete the field and its init.
- [ ] Grep every `cfg.` field. Zero reads → delete from `Config`, `set_defaults`, and the parser.
- [ ] Grep every `Bind` (`B_*`). Zero `bind_hit` calls → delete.

### 4.2 Redundant indirection
- [ ] Single-use wrapper functions: inline them.
- [ ] `#define` aliases used once: replace with the literal.
- [ ] Structs with one field used once: replace with the field.
- [ ] `goto` chains that only jump forward one label: restructure.

### 4.3 Duplication
- [ ] Two loops over cells with the same body: extract only if the extraction is *shorter* than both bodies combined. Usually it isn't — leave duplicated.
- [ ] Repeated `if (errno == …)` in `write_all` and `pump`: they're intentionally separate because they have different retry budgets. Do not merge.
- [ ] The `km[]` table and the `cc[]` table in `handle_keyboard`: keep them as data tables — that's already the minimal form.

### 4.4 Over-engineering smells (reject these in review)
- A `Terminal` struct that wraps `T`.
- A `Renderer` interface with one implementation.
- A `CellAttr` enum mirroring `VTermScreenCell.attrs`.
- Config keys for things no user has asked to change (e.g. `pump_chunk_size`).
- Feature flags (`cfg.enable_foo`) for features that are always on.
- Logging levels. `fprintf(stderr, …)` on failure is enough.

### 4.5 Comments
- Keep the `/* ---- section ---- */` banners. They're the table of contents.
- Keep comments that explain *why* (e.g. `/* keep view stable */`).
- Delete comments that restate the code (`/* increment i */`).

---

## 5. Worked patches

### 5.1 Add "select all" bind (net +6 lines)

```c
/* state */
static Bind B_selall;

/* main, after B_sdown */
B_selall = parse_bind(cfg.key_select_all);

/* input, in the bound chain */
else if (bind_hit(&B_selall, ctrl, shift, alt)) {
    S.active = true;
    S.r0 = 0; S.c0 = 0;
    S.r1 = T.rows - 1; S.c1 = T.cols - 1;
    bound = true;
}
```
No new function. `copy_selection` already reads `S`.

### 5.2 Remove an unused config key (net −N lines)

Say `cfg.letter_spacing` is never read after a refactor:
1. Delete the field from `Config`.
2. Delete its line from `set_defaults`.
3. Delete its parse branch in `load_conf.c`.
4. In `main`, replace `+ cfg.letter_spacing` with `+ 0` — or just drop the `+`.
5. Grep again to confirm zero hits.

Same pattern for any dead `cfg.*`.

### 5.3 Collapse a one-use helper

Before:
```c
static bool is_wide(const VTermScreenCell *c) { return c->width == 2; }
...
if (is_wide(&cell)) { … }
```
After:
```c
if (cell.width == 2) { … }
```
−3 lines, −1 name to remember.

---

## 6. Contributor checklist

Before opening a PR:

- [ ] `grep -n "static" *.c` — every function has a caller.
- [ ] `grep -n "cfg\." *.c` — every key is read.
- [ ] `grep -n "T\.\|S\." *.c` — every field is both written and read.
- [ ] No new file-scope globals unless config-derived singleton.
- [ ] No new struct with <3 fields and one user.
- [ ] No new `#define` used once.
- [ ] Net line count ≤ 0 unless the feature is user-facing and requested. State the justification in the PR description.
- [ ] `render()` still has zero allocations per frame.
- [ ] `handle_keyboard` still drains `GetCharPressed` unconditionally.
- [ ] Cleanup in `main` still frees: scrollback cells, `T.sb`, `T.vt`, font (if not default).

If a checkbox fails, fix it before review. Reviewers will reject on any unchecked box without discussion.

---

## 7. Where NOT to touch

These are load-bearing and small on purpose. Changes here need a concrete bug or a measurable win:

- `write_all` retry logic — bounded, EAGAIN-safe, no allocation.
- `pump` budget — prevents one busy shell from starving rendering.
- `get_cell` branch on `v >= 0` — the entire viewport/scrollback unification depends on it.
- `sel_range` swap — one place, used by `in_sel` and `copy_selection`.
- `render` cursor block ordering — blink → focus → style → draw is the correct precedence.
- Cleanup order in `main` — master close before child kill before waitpid.

If you're refactoring one of these, show the before/after line count and a behavioral test.

---

## 8. Line budget (informational)

Current shape (approximate, do not treat as a target to hit blindly):

| Region | ~lines |
|---|---|
| includes + state | 60 |
| util | 30 |
| keybinds | 45 |
| vterm callbacks | 70 |
| pty | 45 |
| cell access | 30 |
| selection | 70 |
| input | 110 |
| render | 80 |
| font | 40 |
| main | 90 |

If a PR pushes any single region past ~1.5× its current size, the reviewer should ask whether the region needs to be split — and splitting is only allowed if the split *reduces* total lines by removing shared scaffolding, not if it just moves code.