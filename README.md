# kumde

A minimal Wayland compositor written in C.

kumde is built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) and targets a small, readable codebase with deliberate window animations and clean aesthetics. It is not a full desktop environment. It is a compositor you can extend.

---

## Requirements

- wlroots 0.17 or 0.18
- wayland-server
- xkbcommon
- meson + ninja

**Arch Linux**
```
pacman -S wlroots wayland wayland-protocols libxkbcommon meson ninja
```

**Debian / Ubuntu 24.04**
```
apt install libwlroots-dev libwayland-dev libxkbcommon-dev \
            wayland-protocols meson ninja-build
```

**Fedora**
```
dnf install wlroots-devel wayland-devel libxkbcommon-devel \
            wayland-protocols-devel meson ninja-build
```

---

## Building

```
meson setup build
ninja -C build
```

To install system-wide:
```
ninja -C build install
```

---

## Running

From a TTY:
```
./build/kumde
```

Nested inside an existing Wayland session (for development):
```
./build/kumde
```
wlroots will detect the parent compositor automatically and use the `wayland` backend.

On startup, kumde spawns a terminal. It tries `foot`, `alacritty`, `kitty`, and `weston-terminal` in that order.

---

## Keybindings

| Binding         | Action               |
|-----------------|----------------------|
| Super + q       | Quit kumde           |
| Super + Tab     | Cycle window focus   |
| Super + Shift + c | Close focused window |

Click a window to focus it. Windows support drag-to-move and resize via their client-side decorations or xdg_toplevel requests.

---

## Source layout

```
kumde/
├── include/
│   ├── kumde.h      All types and function declarations
│   └── config.h     Compile-time configuration
└── src/
    ├── main.c       Entry point
    ├── server.c     Compositor initialisation and event loop
    ├── output.c     Output management and per-frame rendering
    ├── toplevel.c   XDG toplevel window lifecycle and placement
    ├── input.c      Keyboard and pointer handling
    ├── keybind.c    Keybind registry and default bindings
    ├── border.c     Window border decorations
    ├── anim.c       Animation engine and easing functions
    └── layer.c      Layer shell support
```

---

## Configuration

All tuneable constants live in `include/config.h`. Recompile after changes.

| Constant | Default | Description |
|---|---|---|
| `KUM_BORDER_WIDTH` | `3` | Border thickness in pixels |
| `KUM_BORDER_ACTIVE_R/G/B` | `0.45 / 0.60 / 0.95` | Focused border colour |
| `KUM_BORDER_INACTIVE_R/G/B` | `0.18 / 0.18 / 0.22` | Unfocused border colour |
| `KUM_ANIM_OPEN_MS` | `220.0` | Window open animation duration |
| `KUM_ANIM_CLOSE_MS` | `160.0` | Window close animation duration |
| `KUM_ANIM_FOCUS_MS` | `120.0` | Focus transition duration |
| `KUM_MOD_KEY` | `WLR_MODIFIER_LOGO` | Modifier key (Super) |
| `KUM_CURSOR_SIZE` | `24` | XCursor size |

---

## Animation

`anim.c` implements a delta-time animation engine with no external dependencies.

| Trigger | Curve | Duration |
|---|---|---|
| Window open | ease-out-back (slight spring) | 220 ms |
| Window close | ease-in-cubic | 160 ms |
| Focus change | ease-out-cubic | 120 ms |

Border colour lerps between inactive and active over the focus duration.

---

## Protocols supported

- `xdg-shell` v3
- `wlr-layer-shell-unstable-v1` v4
- `xdg-decoration-unstable-v1`
- `wlr-screencopy-unstable-v1`
- `wp-viewporter`
- `primary-selection-unstable-v1`

---

## Roadmap

- [ ] INI-based config file at runtime
- [ ] Tiling layout mode
- [ ] Workspace switching
- [ ] XWayland support
- [ ] Status bar process (kumbar)
- [ ] Wallpaper via `wlr-layer-shell`
- [ ] Rounded corners and shadow via OpenGL

---

## License

MIT. See `LICENSE`.
