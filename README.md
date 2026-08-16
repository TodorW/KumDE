# kumde

A minimal Wayland compositor written in C, built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots).

Ships with a full companion suite: status bar, app launcher, wallpaper client, screen lock, clipboard persistence, idle/DPMS/autolock daemon, notification daemon, screenshot tool, and a CLI tool for scripting.

---

## Programs

| Binary | Purpose |
|---|---|
| `kumde` | Compositor |
| `kumbar` | Status bar |
| `kumlauncher` | App launcher |
| `kumwall` | Wallpaper client |
| `kumlock` | Screen lock |
| `kumclip` | Clipboard persistence daemon |
| `kumidle` | Idle / DPMS / autolock daemon |
| `kumnotify` | Notification daemon |
| `kumshot` | Screenshot tool |
| `kumde-msg` | IPC CLI tool |

---

## Requirements

**All programs**
- wayland-client / wayland-server
- wayland-protocols
- meson, ninja

**kumde**
- wlroots 0.17 or 0.18
- libxkbcommon
- libinput

**kumbar, kumlauncher, kumwall, kumlock, kumnotify, kumshot**
- cairo
- libxkbcommon (kumlauncher, kumlock)

**kumlock**
- libpam (PAM development headers, e.g. `pam` / `pam-devel` / `libpam0g-dev`)

**Arch Linux**
```
pacman -S wlroots wayland wayland-protocols libxkbcommon libinput cairo pam meson ninja
```

**Debian / Ubuntu 24.04**
```
apt install libwlroots-dev libwayland-dev libxkbcommon-dev libinput-dev \
            libcairo2-dev libpam0g-dev wayland-protocols meson ninja-build
```

**Fedora**
```
dnf install wlroots-devel wayland-devel libxkbcommon-devel libinput-devel \
            cairo-devel pam-devel wayland-protocols-devel meson ninja-build
```

---

## Building

```
meson setup build
ninja -C build
```

With XWayland support:
```
meson setup build -Dxwayland=enabled
ninja -C build
```

Install:
```
ninja -C build install
```

---

## Running

From a TTY:
```
./build/kumde
```

Nested inside an existing Wayland session for development:
```
./build/kumde
```

On startup kumde launches a terminal (`terminal` in config), exports `WAYLAND_DISPLAY` and `KUMDE_IPC`, and optionally starts Xwayland.

---

## Configuration

`$XDG_CONFIG_HOME/kumde/kumde.conf` or `~/.config/kumde/kumde.conf`. Example at `contrib/kumde.conf`.

| Key | Default | Description |
|---|---|---|
| `terminal` | `foot` | Terminal spawned on startup |
| `border_width` | `3` | Border thickness in pixels |
| `border_active` | `0.45 0.60 0.95` | Focused border RGB |
| `border_inactive` | `0.18 0.18 0.22` | Unfocused border RGB |
| `anim_open_ms` | `220` | Window open duration |
| `anim_close_ms` | `160` | Window close duration |
| `anim_focus_ms` | `120` | Focus transition duration |
| `animations` | `true` | Enable or disable all animations |
| `gap` | `8` | Gap between tiled windows |
| `master_ratio` | `0.55` | Master pane width fraction |
| `cursor_size` | `24` | XCursor size |
| `focus_follows_mouse` | `false` | Focus window under pointer on motion |
| `shadows` | `false` | Drop shadows under windows |
| `shadow_radius` | `18` | Shadow blur radius |
| `shadow_alpha` | `0.45` | Shadow opacity |
| `shadow_offset_x` | `0` | Shadow horizontal offset |
| `shadow_offset_y` | `4` | Shadow vertical offset |
| `rounded_corners` | `false` | Rounded window corners (visual only) |
| `corner_radius` | `8` | Corner radius in pixels |
| `xwayland` | `false` | Enable XWayland |

Reload config at runtime: `kill -HUP $(pidof kumde)` or `kumde-msg reload`.

---

## Keybindings

| Binding | Action |
|---|---|
| Super + q | Quit |
| Super + Tab | Cycle window focus |
| Super + Shift + c | Close focused window |
| Super + t | Toggle tile / floating layout |
| Super + Space | Toggle focused window floating |
| Super + 1–9 | Switch workspace on focused output |
| Super + Shift + 1–9 | Move focused window to workspace |

Cursor changes to resize arrows when hovering near window edges. Dragging a tiled window promotes it to floating automatically.

---

## kumbar

Attach to running kumde:
```
kumbar
```

Displays workspace indicators and a clock. Workspace indicators update in real time via the IPC socket. Redraws every second for clock accuracy.

---

## kumlauncher

```
kumlauncher
```

Opens a layer-shell overlay at the top of the screen. Type to filter executables from `$PATH`. Arrow keys to navigate, Enter to launch, Escape to dismiss.

Bind to a key in `keybind.c` or call via `kumde-msg`:
```
kumde-msg launch kumlauncher
```

---

## kumwall

Solid color:
```
kumwall -c 0.08 0.08 0.12
```

PNG image (scaled to fill):
```
kumwall -i /path/to/wallpaper.png
```

Run before kumbar so it sits behind it on the layer stack.

---

## kumlock

```
kumlock
```

Locks all outputs using `ext-session-lock-v1`. Shows the time and a password dot indicator. Password is verified against PAM (`login` service) in `kumlock/src/main.c`.

---

## kumde-msg

Script or bind kumde commands from the shell. `KUMDE_IPC` is exported automatically.

```
kumde-msg workspace 3        # switch to workspace 3
kumde-msg move-to 5          # move focused window to workspace 5
kumde-msg close              # close focused window
kumde-msg layout tile        # set tile layout on focused output
kumde-msg layout float       # set floating layout
kumde-msg reload             # reload config (same as SIGHUP)
kumde-msg quit               # quit kumde
```

---

## Source layout

```
kumde/
├── contrib/kumde.conf
├── include/
│   ├── kumde.h
│   └── config.h
├── src/
│   ├── main.c
│   ├── server.c         init, run, SIGHUP reload
│   ├── output.c         output management, hotplug migration
│   ├── toplevel.c       window lifecycle, fullscreen, maximize
│   ├── input.c          keyboard, pointer, edge cursor hints
│   ├── keybind.c        keybind registry and actions
│   ├── border.c         window border decorations
│   ├── anim.c           animation engine
│   ├── layer.c          layer-shell, usable area
│   ├── workspace.c      per-output workspaces, tiling
│   ├── conf.c           config file parser
│   ├── shadow.c         drop shadow textures
│   ├── ipc.c            Unix socket server, command dispatch
│   └── xwayland.c       XWayland (conditional)
├── kumbar/src/main.c    status bar
├── kumlauncher/src/main.c
├── kumwall/src/main.c
├── kumlock/src/main.c
└── tools/kumde-msg.c
```

---

## Protocols

| Protocol | Used by |
|---|---|
| xdg-shell v3 | kumde |
| wlr-layer-shell-unstable-v1 | kumde, kumbar, kumlauncher, kumwall |
| ext-session-lock-v1 | kumlock |
| xdg-decoration-unstable-v1 | kumde |
| wlr-screencopy-unstable-v1 | kumde |
| wp-viewporter | kumde |
| primary-selection-unstable-v1 | kumde |
| xdg-output-unstable-v1 | kumbar |

---

## License

MIT. See `LICENSE`.
