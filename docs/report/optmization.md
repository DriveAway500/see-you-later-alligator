# Performance, Memory, and Dependency Cost Audit

## Scope and method

This is a static review of the current source, configuration limits, build file, and dependency boundaries. No CPU, RAM, GPU, or VRAM profiler was run, so machine-specific measurements and driver behavior are intentionally not inferred. Findings below describe code paths and their likely impact under the stated workloads.

## Findings

### High: scrollback can consume substantial process RAM

`term_init` allocates one `SbLine` descriptor for every configured scrollback line. Each pushed line then gets its own allocation containing a copy of the row's `VTermScreenCell` values. The cell payload therefore grows approximately as:

`retained lines * terminal columns * sizeof(VTermScreenCell)`

This excludes the descriptor array and allocator overhead. The default is 10,000 lines; the configuration accepts up to 1,000,000 lines and 1,000 columns. The descriptor array is allocated eagerly, while row-cell storage grows as history fills. Once the ring is full, every new line frees and reallocates a row, adding allocator churn during sustained scrolling. See [term.c](../../src/term.c#L18) and [load_conf.c](../../src/load_conf.c#L100).

**Recommendation:** enforce a byte budget rather than only a line count; reuse existing row allocations when dimensions permit. Consider reporting when the requested history cannot be allocated instead of silently disabling it.

### High during output: rendering revisits the entire visible grid

`render_frame` loops over every visible row and column, fetches each cell, resolves colors and attributes, and submits background and glyph drawing operations. A PTY read that processes output forces a redraw. There is also a safety redraw every 0.5 seconds, even while otherwise idle; the default frame target is 30 FPS. Raylib may batch graphics operations, so the exact GPU draw-call count is backend-dependent, but the per-cell CPU traversal and API calls are present in the source. See [render.c](../../src/render.c#L366) and [main.c](../../src/main.c#L132).

**Recommendation:** use libvterm damage notifications to track changed rows or spans and redraw only damaged regions. Profile before implementing more complex batching; the current code already caches color tables and glyph indices.

### Medium, conditional: PTY writes can stall the event/render thread

`pty_write_all` retries `EAGAIN` by polling in 50 ms intervals for up to 99 attempts, roughly five seconds. It is called by normal input paths and by libvterm's output callback, which can run while PTY bytes are being parsed. A child process that is not reading can therefore freeze input and drawing. The function returns `void`, so callers cannot detect a timeout or partial write; remaining bytes are silently lost. See [pty_io.c](../../src/pty_io.c#L40) and [term.c](../../src/term.c#L79).

**Recommendation:** queue pending output and flush it when the PTY is writable, without blocking the main loop. Return an explicit status and handle partial writes and poll errors.

### Medium under sustained output: parsing has a fixed per-iteration budget

`term_pump` reads and parses at most 512 KiB per call. This bounds one iteration's work, but continuous output can arrive faster than it is consumed and make the display lag. At 30 redraws per second, the cap alone corresponds to a theoretical ceiling of about 15 MiB/s before parsing, drawing, and scheduling overhead; this is not a measured throughput figure. See [term.c](../../src/term.c#L132).

**Recommendation:** measure backlog and responsiveness with representative workloads (for example, large file output and fast progress updates), then tune the budget. Preserve input responsiveness if terminal processing is later moved off the main thread.

### Medium for large selections: copy allocates a pessimistic temporary buffer

`sel_text` reserves up to four bytes for every possible code point in every selected cell, multiplied by `VTERM_MAX_CHARS_PER_CELL`, plus row separators. Most cells use much less space, so selecting a large viewport can temporarily reserve considerably more RAM than the copied text requires. This is user-triggered rather than per-frame work. See [selection.c](../../src/selection.c#L30).

**Recommendation:** grow the buffer as encoded text is emitted, or perform a sizing pass before allocation.

### Low: active selection adds repeated per-cell range work

For every rendered cell while selection is active, `sel_contains` recomputes the ordered endpoints before checking membership. This adds work proportional to the visible grid on every redraw with a selection. See [render.c](../../src/render.c#L385) and [selection.c](../../src/selection.c#L14).

**Recommendation:** compute the ordered endpoints once per frame, or derive selected column intervals per row.

### Low baseline CPU: idle loop wakes frequently

When it has nothing to draw, the main loop polls raylib input and waits for 8 ms, or about 125 wakeups per second before OS timer coalescing. The 0.5-second safety redraw adds two frames per second while idle. This is not a busy spin, and actual idle CPU impact depends on the window backend and system. See [main.c](../../src/main.c#L13).

**Recommendation:** only change the wait strategy if idle power or CPU measurements show this is material. A wait integrated with both input and PTY readiness would reduce polling, but requires a backend-aware event design.

### Low, event-driven: font loading and zoom have transient texture cost

At startup and on font-size changes, raylib loads a font with a fixed set of roughly 1,900 requested codepoints. During zoom, the new font is loaded before the old font is freed, briefly keeping both glyph data and textures alive. This cost occurs on startup or an explicit zoom, not per frame. Actual atlas dimensions and VRAM use depend on the font and raylib/graphics backend. See [render.c](../../src/render.c#L149) and [render.c](../../src/render.c#L214).

**Recommendation:** leave this alone unless startup latency or zoom spikes are observed. If they are, reduce the requested glyph set or cache a small number of font sizes, accounting for the additional retained texture memory.

## GPU and VRAM considerations

- The scrollback ring and libvterm cell data are CPU-side allocations; they do not consume VRAM directly.
- The renderer's clearest application-owned GPU resource is the raylib font texture. The window backbuffer/swapchain and driver-managed resources also consume graphics memory, but their sizes and buffering are backend-dependent.
- The requested window width is `columns * cell_width + 2 * padding`; its height is `rows * cell_height + 2 * padding`. Their product determines the pixel area before accounting for buffering. Large accepted values can request an impractically large window and framebuffer. The source does not impose a total pixel-area limit.
- A transparent window is requested when opacity is below 1. Compositing may add work, but the impact depends on the window system and compositor. Cell colors themselves are still drawn opaque.
- There is no evidence in this source of a per-cell texture allocation. Drawing many glyphs can still cost CPU time and graphics bandwidth even when raylib batches operations.

## Dependency-related costs

These are local native libraries, not hosted services: the code does not make network requests or incur a recurring service charge through raylib, libvterm, `libutil`, or `libm`.

| Component | Runtime role and likely cost |
| --- | --- |
| raylib | Owns window/input polling, clipboard calls, font loading, drawing, and frame pacing. It is the main graphics-facing dependency and owns font texture resources. Backend libraries and compositor costs vary by system. |
| libvterm | Parses terminal output, maintains the live screen/state, converts input, and invokes callbacks. CPU cost scales with terminal output and screen changes; its live screen is additional process RAM. |
| PTY, child shell, and Linux process APIs | Kernel buffering and the shell/application running inside the terminal. The child workload can dominate CPU and memory and is separate from the emulator's own resource use. |
| `libutil` | Link-time support for `forkpty`; no significant ongoing computation is apparent in the application source. |
| `libm` | Used for small numeric operations such as rounding and wheel calculations; no meaningful standalone runtime cost is apparent. |

The Makefile obtains raylib and libvterm flags through `pkg-config`, with a linker-name fallback, and links `-lutil -lm`. Exact shared-library dependencies and resident memory depend on the distribution and raylib build. Shared library pages may be shared with other processes; package disk size and graphics-driver allocations are installation-specific. See [makefile](../../makefile#L12) and [silly-guide.md](../silly-guide.md#L118).

## Suggested measurements

Profile representative idle, scrollback-heavy, full-screen application, and fast-output workloads. `perf stat` can compare CPU time and context switches; `/usr/bin/time -v` can record peak resident memory; Massif or heaptrack can attribute heap growth; GPU monitoring tools such as `intel_gpu_top`, `nvidia-smi`, or the relevant AMD tool can observe device-level usage. Use the tools appropriate to the target machine and compare builds with the same workload. No profiler results are included in this report.
