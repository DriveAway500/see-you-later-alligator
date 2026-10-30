# Project Inconsistency Audit

## Scope

This report compares the current source, headers, Makefile, sample configuration, README, and source guide. Findings are based on static inspection; no runtime tests were performed. Severity reflects user-visible impact and likelihood, not security classification.

## Findings

### Medium: switching build modes may leave the root executable pointing at another mode

The root `silly-term` target depends on the selected mode's binary, and its symlink recipe runs only when Make considers that prerequisite newer. If the requested mode's binary already exists but the root symlink is newer, `make MODE=debug` or `make MODE=asan` can leave the symlink pointing at the previous mode. This conflicts with the Makefile comment and README statement that the root path follows the selected build. See [makefile](../../makefile#L45) and [README.md](../../README.md#L39).

**Suggested correction:** make mode selection update the symlink unconditionally or use a mode-specific launch target that does not rely on timestamps.

### Medium: a failed `execvp` is reported as a successful terminal start

`forkpty` returns a positive child PID before the child calls `execvp`. If `execvp` fails, the child prints an error and exits with status 127, but the parent still treats the positive PID as successful initialization. `main` therefore does not print its `cannot start shell` diagnostic and can exit normally after observing PTY closure. See [pty_io.c](../../src/pty_io.c#L31), [term.c](../../src/term.c#L114), and [main.c](../../src/main.c#L104).

**Suggested correction:** add a close-on-exec error pipe so the parent can distinguish successful `exec` from child startup failure.

### Medium: nonblocking PTY setup errors are ignored

`pty_spawn` documents that the parent PTY is made nonblocking, but ignores the results of both `F_GETFL` and `F_SETFL`. If this setup fails, `term_pump` can block in `read` after consuming available output, freezing the event loop. See [pty_io.h](../../src/headers/pty_io.h#L6), [pty_io.c](../../src/pty_io.c#L36), and [term.c](../../src/term.c#L136).

**Suggested correction:** check both calls, close the descriptor and terminate/reap the child on failure, and propagate the error.

### Medium: initialization failure bypasses resource cleanup

`term_init` can fail after allocating scrollback or creating libvterm state. `main` returns immediately without calling `term_free`, `view_free`, or `CloseWindow`. The operating system reclaims process memory at exit, but graphics/window resources are not explicitly released and the module lifecycle contract is incomplete. See [term.c](../../src/term.c#L99), [term.c](../../src/term.c#L104), and [main.c](../../src/main.c#L104).

**Suggested correction:** use a single cleanup path with initialization-state checks, and make `term_init` release its own partial state on failure.

### Medium: floating-point `NaN` bypasses configured bounds

The float parser accepts `nan` through `strtod`. Both range comparisons are false for NaN, so it is stored rather than clamped or rejected. This contradicts the guide's claim that floating-point values are clamped and can pass an invalid opacity or other non-finite value into rendering/window setup. See [load_conf.c](../../src/load_conf.c#L241) and [silly-guide.md](../silly-guide.md#L82).

**Suggested correction:** reject values for which `isfinite(value)` is false before storing them.

### Medium: PTY write contract hides incomplete writes and errors

The header describes `pty_write_all` as writing everything with bounded retries, but the function returns `void` and exits on timeout or other write errors. Callers cannot distinguish full success from a partial or dropped write. The guide notes the lack of a status return, but the public API name/comment still suggests completion. See [pty_io.h](../../src/headers/pty_io.h#L11), [pty_io.c](../../src/pty_io.c#L40), and [silly-guide.md](../silly-guide.md#L106).

**Suggested correction:** return a status and byte count, and update callers and documentation to describe partial/error behavior.

### Medium: child reaping is only attempted once without waiting

`term_free` sends `SIGHUP` and immediately calls `waitpid(..., WNOHANG)`. If the child has not exited at that instant, this code does not wait for it again or confirm it terminated before the parent exits. A child that ignores or delays handling the signal can therefore outlive the terminal process. The guide says shutdown signals the child but does not describe this best-effort termination/reaping behavior. See [term.c](../../src/term.c#L119).

**Suggested correction:** wait for the child with a bounded policy, escalating only if it fails to exit; ensure any forced termination is also reaped.

### Low: documented default FPS is 60, implementation default is 30

The configuration reference lists `fps` as `60`, while `set_defaults` and the checked-in sample configuration use `30`. This makes the reference inconsistent with both actual default paths. See [silly-guide.md](../silly-guide.md#L90), [load_conf.c](../../src/load_conf.c#L34), and [sillyconf.conf](../../sillyconf.conf#L22).

**Suggested correction:** change the guide's table to 30, or change the code and sample if 60 is the intended default.

### Low: invalid cursor styles silently fall back to block

`cursor_style` is stored as an unchecked string. Rendering recognizes only `underline` and `bar`; every other value, including a typo, becomes block without a parser warning. The guide lists accepted styles but generally states malformed values are reported. See [load_conf.c](../../src/load_conf.c#L42), [render.c](../../src/render.c#L192), and [silly-guide.md](../../docs/silly-guide.md#L92).

**Suggested correction:** validate the string while loading configuration and report the path and line for unsupported values.

### Low: key binding alternatives and strings can be silently truncated

`BindSet` stores at most four alternatives, and `parse_bindset` stops parsing at that limit. The configuration guide and sample describe comma-separated alternatives without documenting the cap. Configuration strings are also copied into fixed-size fields, and the parser does not report truncation. Invalid key tokens are dropped by `parse_bindset` without a diagnostic. See [keybind.h](../../src/headers/keybind.h#L8), [keybind.c](../../src/keybind.c#L70), and [load_conf.c](../../src/load_conf.c#L197).

**Suggested correction:** report excess/invalid alternatives and truncation, or document and enforce explicit limits in the configuration reference.

### Low: requested scrollback can silently be disabled on allocation failure

If the descriptor-array allocation fails, `term_init` sets `sb_cap` to zero and continues successfully. The configured history is then unavailable without a warning, although the configuration and guide present the setting as an active retained-line limit. See [term.c](../../src/term.c#L99).

**Suggested correction:** emit a warning or fail initialization when a nonzero requested scrollback cannot be allocated.

### Low: maximum cursor thickness can exceed the cursor cell

`cursor_thickness` accepts values from 1 to 20 independently of font size and line height. The renderer uses it directly for underline/bar dimensions, so a valid configuration can draw the cursor beyond a small cell. See [load_conf.c](../../src/load_conf.c#L45) and [render.c](../../src/render.c#L433).

**Suggested correction:** clamp thickness to the current cell width/height at render time, or constrain it based on calculated cell metrics.

### Low: long configuration lines are split without a diagnostic

The parser uses a 1,024-byte `fgets` buffer and treats each chunk as a complete line. A longer setting is therefore parsed as multiple lines, potentially producing misleading errors or truncated values instead of a clear line-length diagnostic. The documented format does not state this limit. See [load_conf.c](../../src/load_conf.c#L197).

**Suggested correction:** detect a missing newline before EOF and report a line-too-long error, or use a dynamically sized line reader.

## Cross-checks

- The README, sample config, and source agree on the default 30 FPS; the mismatch is in the source guide.
- The shell command is intentionally split on spaces rather than interpreted by a shell, and the guide documents that behavior. However, the command is copied through a 256-byte local buffer and argument parsing is capped at 31 arguments; only the argument cap is documented.
- `term_pump`'s 512 KiB budget and nonblocking-read expectation are documented; the unchecked nonblocking setup is the implementation mismatch.
- The Makefile links raylib, libvterm, `libutil`, and `libm` as described in the guide. No unused configuration fields were identified in the static review.
