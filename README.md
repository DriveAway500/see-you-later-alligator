# silly-term

<p align="center">
  <img src="assets/silly-term.png" alt="silly-term terminal emulator" width="49%">
  <img src="assets/silly-term-htop.png" alt="silly-term running htop" width="49%">
</p>

<p align="center">
  <strong>A lightweight, event-driven terminal emulator for Linux.</strong>
  <br>
  Built in C with GLFW, OpenGL, FreeType, and libvterm.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C11-00599C?style=flat-square&logo=c" alt="C11">
  <img src="https://img.shields.io/badge/platform-Linux-FCC624?style=flat-square&logo=linux&logoColor=black" alt="Linux">
  <img src="https://img.shields.io/badge/graphics-OpenGL-5586A4?style=flat-square&logo=opengl" alt="OpenGL">
  <img src="https://img.shields.io/badge/build-GNU%20Make-427819?style=flat-square&logo=gnu" alt="GNU Make">
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#technology-stack">Technology Stack</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#configuration">Configuration</a> ·
  <a href="#architecture">Architecture</a>
</p>

---

## Overview

**silly-term** is a small, native Linux terminal emulator written in C.

It combines GLFW and OpenGL for window management and rendering, FreeType for font rasterization, libvterm for terminal emulation, and a pseudo-terminal (PTY) for shell communication.

The project focuses on a straightforward architecture, efficient resource usage, and predictable behavior. Its implementation is intentionally kept small and readable, without sacrificing the terminal features needed for everyday use.

## Technology Stack

| Component | Technology | Purpose |
|:--|:--|:--|
| Language | C11 | Core implementation |
| Windowing | GLFW 3 | Window creation and input |
| Graphics | OpenGL | Terminal rendering |
| Fonts | FreeType 2 | Font loading and rasterization |
| Terminal | libvterm | Terminal state and escape sequences |
| Process I/O | POSIX PTY | Shell communication |
| Build system | GNU Make | Compilation and installation |

## Features

- **Terminal emulation** — Truecolor, indexed colors, and configurable 16-color palette.
- **Scrollback** — Bounded history, mouse selection, clipboard integration, and copy-on-select.
- **Alternate screen** — Compatible with interactive applications such as `vim`, `less`, and `htop`.
- **Font management** — Configurable monospace fonts, runtime font resizing, and glyph caching.
- **Background rendering** — Custom background images and transparency.
- **Mouse integration** — Mouse reporting for applications that request it, with `Shift` reserved for text selection.
- **Window management** — Resizing, fullscreen mode, and undecorated windows.
- **Keyboard shortcuts** — Configurable key bindings and clipboard operations.
- **Event-driven execution** — PTY notifications and event-based rendering, avoiding busy polling while idle.

## Installation

### Dependencies

Required development packages:

- GCC or Clang
- GNU Make
- pkg-config
- GLFW 3
- FreeType 2
- libvterm
- OpenGL development headers
- fontconfig and a monospace font

**Debian / Ubuntu**

```sh
sudo apt install \
    build-essential \
    pkg-config \
    libglfw3-dev \
    libfreetype-dev \
    libvterm-dev \
    libgl-dev \
    fonts-dejavu-core \
    fontconfig
```

### Build

Clone the repository and compile:

```sh
git clone https://github.com/DriveAway500/see-you-later-alligator.git
cd see-you-later-alligator

make
```

Launch:

```sh
./silly-term
```

The default build mode is `release`.

Additional build modes:

```sh
make MODE=debug
make MODE=asan
```

Build artifacts are stored under `build/<mode>/`. The root executable is a symlink to the selected build.

## Configuration

silly-term uses a plain-text configuration file named `sillyconf.conf`.

Specify a custom configuration:

```sh
./silly-term -c /path/to/sillyconf.conf
```

Otherwise, the configuration is loaded from:

```text
$XDG_CONFIG_HOME/silly-term/sillyconf.conf
```

If `XDG_CONFIG_HOME` is not defined:

```text
$HOME/.config/silly-term/sillyconf.conf
```

When no configuration file is available, built-in defaults are used.

Install the sample configuration without overwriting an existing file:

```sh
make install-conf
```

The configuration format supports:

- `key = value` assignments.
- Comments beginning with `#` or `;`.
- Boolean values: `true`, `false`, `yes`, `no`, `on`, `off`, `1`, and `0`.
- Three-digit and six-digit RGB hexadecimal colors.
- Numeric values clamped to supported ranges.

See [`sillyconf.conf`](sillyconf.conf) for the complete configuration reference.

## Default Keybindings

| Action | Shortcut |
|:--|:--|
| Copy | `Ctrl+Shift+C` |
| Paste | `Ctrl+Shift+V` / `Shift+Insert` |
| Select all | `Ctrl+Shift+A` |
| Scroll up / down | `Shift+PageUp` / `Shift+PageDown` |
| Scroll to top / bottom | `Shift+Home` / `Shift+End` |
| Clear scrollback | `Ctrl+Shift+K` |
| Increase font size | `Ctrl+=` / `Ctrl+Shift+=` / `Ctrl+KP_Add` |
| Decrease font size | `Ctrl+-` / `Ctrl+KP_Subtract` |
| Reset font size | `Ctrl+0` / `Ctrl+KP_0` |
| Toggle fullscreen | `F11` |

Additional mouse controls:

| Action | Input |
|:--|:--|
| Select text | Left-click and drag |
| Paste clipboard | Middle-click |
| Scroll | Mouse wheel |

When running alternate-screen applications, the mouse wheel sends navigation input to the application. Applications with mouse reporting enabled receive mouse events directly unless `Shift` is held.

## Shell Execution

The configured shell command is executed directly through a PTY.

Arguments are separated using spaces and tabs. Shell syntax such as quoting, variable expansion, and pipelines is not interpreted.

`Esc` is forwarded to the shell rather than being used to close the terminal window.

To exit, terminate the shell normally (for example, with `Ctrl+D`) or close the window through your window manager.

## Architecture

The source tree is organized by responsibility:

```text
src/
├── headers/
├── main.c
├── window.c
├── input.c
├── keybind.c
├── term.c
├── pty_io.c
├── pty_watch.c
├── render.c
├── glyph.c
└── image.c
```

| Module | Responsibility |
|:--|:--|
| `main.c` | Application initialization and event loop |
| `window.c` | GLFW window management and resizing |
| `input.c` | Keyboard and mouse input |
| `keybind.c` | Shortcut handling |
| `term.c` | libvterm integration and terminal state |
| `pty_io.c` | PTY communication and child process I/O |
| `pty_watch.c` | PTY notifications and event-loop wakeups |
| `render.c` | OpenGL rendering |
| `glyph.c` | Font rasterization and glyph caching |
| `image.c` | Background image loading and rendering |

Public interfaces are defined under `src/headers/`.

For a deeper explanation of the runtime, module responsibilities, configuration options, and build process, see the [Technical Guide](docs/silly-guide.md).

## License

Licensed under the MIT License. See [LICENSE](LICENSE).

---

<p align="center">
  <sub>Built with C, OpenGL, and a reasonable amount of stubbornness.</sub>
</p>