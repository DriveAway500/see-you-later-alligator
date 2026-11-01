# Complete Guide to sillyconf.conf: Configuring silly-term

This guide explains every option available in `sillyconf.conf`, the configuration file for silly-term. You will learn where the file lives, how its syntax works, and what each setting does.

## 1. Where the configuration file lives

By default, silly-term looks for its configuration at:

```
$XDG_CONFIG_HOME/silly-term/sillyconf.conf
```

If `XDG_CONFIG_HOME` is not set, it falls back to:

```
~/.config/silly-term/sillyconf.conf
```

You can also point silly-term at a specific file from the command line:

```sh
silly-term -c /path/to/file.conf
```

If the file does not exist, silly-term starts with built in defaults. Every line in the reference file shows the default value, so you can delete any line you do not want to change.

## 2. Syntax rules

The format is simple. Each setting is one line:

```
key = value
```

Rules to keep in mind:

- Lines starting with `#` or `;` are comments.
- You can also place an inline comment after a value using whitespace followed by `#`, for example `font_size = 20   # px`. A color like `#c5c8c6` is not treated as a comment because there is no whitespace before the `#`.
- Quotes around values are optional. `title = silly-term` and `title = "silly-term"` are equivalent.
- Booleans accept `true`, `false`, `yes`, `no`, `on`, `off`, `1`, and `0`. All are case insensitive.
- Colors use `#rrggbb` or the short form `#rgb`.
- Numeric values outside the allowed range are clamped to the nearest limit. They do not cause an error.
- Unknown keys produce a warning on stderr and are ignored.
- A value that fails to parse (for example a non integer in an integer field) produces a warning and that line is skipped, leaving the previous or default value in place.

## 3. Font settings

```
font_path        =
font_size        = 20
line_height      = 1.2
letter_spacing   = 0
font_smooth      = true
font_step        = 2
```

**font_path** is the path to the main font file. When empty, silly-term tries common system monospace fonts such as DejaVu Sans Mono, Liberation Mono, and Noto, then falls back to fontconfig's default monospace. Other installed fonts act as fallbacks for glyphs the main font lacks.

**font_size** sets the glyph height in pixels. Allowed range is 6 to 200.

**line_height** is a multiplier applied to `font_size` to determine the cell height. Allowed range is 0.5 to 3.0.

**letter_spacing** adds extra pixels per cell. Allowed range is -10 to 50. Negative values tighten the spacing.

**font_smooth** toggles antialiasing. Set it to `false` for crisp one bit glyphs, which some users prefer on low DPI displays.

**font_step** is how many pixels the font size changes per zoom shortcut. Allowed range is 1 to 20.

## 4. Window settings

```
title            = silly-term
cols             = 80
rows             = 24
padding          = 4
decorated        = true
resizable        = true
opacity          = 1.0
fps              = 30
```

**title** is the initial window title.

**cols** and **rows** set the initial grid size in character cells. `cols` allows 2 to 1000, `rows` allows 1 to 1000.

**padding** is the number of pixels between the grid and the window edge. Allowed range is 0 to 200.

**decorated** controls whether the window keeps its title bar and borders. Set it to `false` for a borderless look, useful with tiling window managers.

**resizable** allows or forbids resizing the window.

**opacity** sets background transparency from 0.0 to 1.0. It affects the background only and requires a running compositor.

**fps** caps redraws per second, from 5 to 360. Frames are only drawn when something changes, so a higher value does not waste CPU while idle.

## 5. Background image settings

```
bg_image         =
bg_image_mode    = cover
bg_image_opacity = 1.0
```

**bg_image** is the path to an image drawn behind the text. Supported formats include PNG, JPEG, BMP, GIF, and TGA. The `~/` prefix is expanded. When empty or when the file is missing, the background color from `bg` is used.

**bg_image_mode** controls how the image fits the window. Valid values are:

- `cover` fills the window, cropping as needed.
- `contain` fits the whole image inside the window.
- `stretch` distorts the image to fill the window exactly.
- `center` draws the image at its original size, centered.
- `tile` repeats the image.

**bg_image_opacity** sets the image transparency from 0.0 to 1.0. The final effect is also scaled by the window `opacity`.

## 6. Shell settings

```
shell            = $SHELL
term             = xterm-256color
```

**shell** is the command line started inside the terminal. It is split on spaces, so quoting is not supported. When unset, silly-term uses the `SHELL` environment variable, and if that is empty it uses `/bin/bash`.

**term** is the value exported as `TERM` to the shell and its children. Leave it at `xterm-256color` unless you have a reason to change it.

## 7. Scrolling settings

```
scrollback       = 10000
scroll_lines     = 3
```

**scrollback** is how many lines of history are kept in memory. Allowed range is 0 to 1000000. Set it to 0 to disable scrollback entirely.

**scroll_lines** is how many lines one wheel notch scrolls. Allowed range is 1 to 100.

## 8. Cursor settings

```
cursor_style     = block
cursor_blink     = true
cursor_blink_ms  = 600
cursor_blink_timeout = 15
cursor_thickness = 2
```

**cursor_style** accepts `block`, `underline`, or `bar`.

**cursor_blink** enables or disables cursor blinking.

**cursor_blink_ms** is the blink interval in milliseconds, from 100 to 5000.

**cursor_blink_timeout** is how many seconds of no input pass before the blink stops, letting an idle terminal sleep. Range is 0 to 3600. A value of 0 means the cursor blinks forever.

**cursor_thickness** is the thickness in pixels for underline and bar cursors. Range is 1 to 20. It has no effect on a block cursor.

## 9. Text and behavior settings

```
bold_bright      = true
fake_bold        = false
copy_on_select   = false
mouse_reporting  = true
```

**bold_bright** makes bold text using palette colors 0 to 7 switch to colors 8 to 15 for extra contrast.

**fake_bold** draws bold glyphs twice, one pixel apart, for fonts that lack a real bold face.

**copy_on_select** copies the selected text to the clipboard automatically when a drag selection ends.

**mouse_reporting** forwards mouse events to applications that request them, such as vim, tmux, and htop. Hold Shift while dragging to select text anyway.

## 10. Color settings

```
fg               = #c5c8c6
bg               = #1d1f21
cursor_color     = #c5c8c6
selection_fg     = #ffffff
selection_bg     = #373b41
```

**fg** is the default text color. **bg** is the default background color, also used behind `bg_image`.

**cursor_color** is the cursor color.

**selection_fg** and **selection_bg** are the text and background colors for selected text.

## 11. The 16 color palette

```
color0  = #1d1f21
color1  = #cc6666
color2  = #b5bd68
color3  = #f0c674
color4  = #81a2be
color5  = #b294bb
color6  = #8abeb7
color7  = #c5c8c6
color8  = #969896
color9  = #cc6666
color10 = #b5bd68
color11 = #f0c674
color12 = #81a2be
color13 = #b294bb
color14 = #8abeb7
color15 = #ffffff
```

Colors 0 to 7 are the normal ANSI colors. Colors 8 to 15 are the bright variants. Programs that emit ANSI color codes map them to these entries. Changing them changes how your shell prompt, `ls`, `git`, and other tools look. The defaults here follow the Tomorrow Night scheme.

## 12. Keybind settings

```
key_copy             = ctrl+shift+c
key_paste            = ctrl+shift+v, shift+insert
key_select_all       = ctrl+shift+a
key_scroll_up        = shift+pageup
key_scroll_down      = shift+pagedown
key_scroll_top       = shift+home
key_scroll_bottom    = shift+end
key_clear_scrollback = ctrl+shift+k
key_font_increase    = ctrl+equal, ctrl+shift+equal, ctrl+kp_add
key_font_decrease    = ctrl+minus, ctrl+kp_subtract
key_font_reset       = ctrl+0, ctrl+kp_0
key_fullscreen       = f11
```

Each keybind accepts up to four alternatives separated by commas. Modifiers are `ctrl`, `shift`, and `alt`. Supported keys include `a` through `z`, `0` through `9`, `f1` through `f12`, `pageup`, `pagedown`, `home`, `end`, `insert`, `delete`, `up`, `down`, `left`, `right`, `space`, `enter`, `tab`, `backspace`, `equal`, `plus`, `minus`, `comma`, `period`, `slash`, `kp_add`, `kp_subtract`, and `kp_0`.

The meaning of each action:

- **key_copy** copies the current selection.
- **key_paste** pastes from the clipboard.
- **key_select_all** selects the entire scrollback buffer.
- **key_scroll_up** and **key_scroll_down** scroll the view by `scroll_lines`.
- **key_scroll_top** and **key_scroll_bottom** jump to the beginning or end of scrollback.
- **key_clear_scrollback** discards the saved history.
- **key_font_increase** and **key_font_decrease** change the font size by `font_step`.
- **key_font_reset** restores the configured font size.
- **key_fullscreen** toggles fullscreen mode.

## 13. Practical examples

Here are a few small snippets you can copy into your file.

A minimal dark setup with a background image:

```
bg_image         = ~/Pictures/wallpaper.png
bg_image_mode    = cover
bg_image_opacity = 0.8
opacity          = 0.95
fg               = #e0e0e0
bg               = #101418
```

A borderless terminal for a tiling window manager:

```
decorated        = false
padding          = 0
resizable        = true
```

A larger grid for a wide monitor with a comfortable font:

```
cols             = 140
rows             = 40
font_size        = 18
line_height      = 1.25
```

Quieter cursor and more scrollback:

```
cursor_style     = bar
cursor_blink     = false
scrollback       = 50000
```

## 14. Testing and troubleshooting

After editing the file, restart silly-term or start a new instance with `silly-term -c ~/.config/silly-term/sillyconf.conf` to test without replacing your running session.

If a setting seems ignored, check stderr for warnings. The loader prints a line like `path:line: unknown key 'foo'` or `path:line: 'font_size' is not an integer` for each problem line. Fixing those warnings is usually enough to get the desired behavior.

Remember that numeric values are clamped, so `font_size = 9999` silently becomes 200 rather than failing. If a value does not seem to take effect, confirm it is within the documented range.

For colors, use six digit hex to be safe. The short three digit form works, but tools that generate config files sometimes produce it in unexpected ways.

For keybinds, avoid binding a combination that your shell or terminal applications already rely on. If a binding stops working after an update to the application running inside the terminal, check whether that program has added its own handler for the same combination.