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
- wlroots 0.17, 0.18, 0.19 or 0.20
- libxkbcommon
- libinput

**kumbar, kumlauncher, kumwall, kumlock, kumnotify, kumshot**
- cairo
- libxkbcommon (kumlauncher, kumlock)

**kumlock**
- libpam (PAM development headers, e.g. `pam` / `pam-devel` / `libpam0g-dev`)

**Optional, runtime only (media keys)**
- `wpctl` (wireplumber) for volume keys
- `brightnessctl` for brightness keys

**Arch Linux**
```
pacman -S wlroots0.20 wayland wayland-protocols libxkbcommon libinput cairo pam meson ninja
```
Arch's wlroots package name is version-suffixed and changes as wlroots releases (currently `wlroots0.20`); run `pacman -Ss wlroots` if this stops matching.

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

`ninja -C build install` also installs a `wayland-sessions/kumde.desktop` entry, so display managers that scan that directory (greetd + a greeter, gdm, sddm, ...) can offer "KumDE" as a login session.

---

## Session integration

The simplest way to start the companion daemons is kumde's own `autostart` config key (see `contrib/kumde.conf`) -- no session manager or systemd required:
```
autostart = kumbar
autostart = kumclip
autostart = kumnotify
autostart = kumidle --lock-s 300 --dpms-s 360
```

If you'd rather manage them as systemd user services (independent restart/logging), unit files are in `contrib/systemd/`. They default to `ExecStart=%h/.local/bin/<binary>`, so either install with that prefix:
```
meson setup build --prefix="$HOME/.local"
ninja -C build install
```
or edit the `ExecStart=` path in the unit file to match wherever you installed to. Then, per service:
```
systemctl --user enable --now kumbar.service
```
There is no unit file for `kumde` itself -- it's meant to be launched from a TTY or a display manager entry, not as a systemd service.

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

Reload config at runtime: `kill -HUP $(pidof kumde)` or `kumde-msg reload`. Reload re-reads every key above plus `[input]`, `[output NAME]` and `[rules]` below and re-applies them live -- nothing requires a restart except `terminal`, `xwayland`, and `autostart` (those only run once, at startup).

Three additional sections, all optional:

**`[rules]`** -- auto-assign workspace/floating/size/position/fullscreen per window, matched by glob against app_id (or WM_CLASS for XWayland) and/or title. First matching rule wins per window.
```
[rules]
rule = app_id:firefox, workspace:2
rule = app_id:steam, floating:true
rule = title:*Picture-in-Picture*, floating:true, size:640x360
```

**`[input]`** -- libinput device settings, applied to every pointer device.
```
[input]
tap_to_click       = true
natural_scroll     = false
disable_while_typing = true
pointer_accel      = 0.0
kb_layout          = us
kb_variant         =
kb_model           =
kb_options         =
```
`kb_layout` accepts a comma-separated list (`us,rs`); Super+k cycles through them at runtime.

**`[output NAME]`** -- one section per output (name from `wlr-randr` / `kumde-msg subscribe`'s output field), applied when that output connects and on every reload.
```
[output DP-1]
mode      = 1920x1080@60
pos       = 0,0
scale     = 1.0
transform = normal
enabled   = true
```

A section ends at the next `[...]` header (or end of file), so keep global keys and `autostart` lines above the first section header -- see `contrib/kumde.conf`.

---

## Keybindings

| Binding | Action |
|---|---|
| Super + q | Quit |
| Super + Tab | Cycle window focus |
| Super + Shift + c | Close focused window |
| Super + t | Toggle tile / floating layout |
| Super + m | Monocle layout |
| Super + Space | Toggle focused window floating |
| Super + Shift + f | Toggle fullscreen on focused window |
| Super + 1–9 | Switch workspace on focused output (press again to return to previous) |
| Super + Shift + 1–9 | Move focused window to workspace |
| Super + ` | Toggle scratchpad workspace |
| Super + Left / Right / Up / Down | Focus adjacent output |
| Super + Return | Swap focused window into master |
| Super + Shift + Left / Right | Shrink / grow master pane (tiled) or window width (floating) |
| Super + Shift + Up / Down | Shrink / grow window height (floating; no-op in tiled layout) |
| Super + k | Cycle to next configured keyboard layout |
| Super + Print | Screenshot (spawns `kumshot`) |
| Super + Shift + q | Power off |
| Super + Shift + l | Lock screen (spawns `kumlock`) |
| XF86AudioRaiseVolume / LowerVolume / Mute | Volume via `wpctl` |
| XF86MonBrightnessUp / Down | Brightness via `brightnessctl` |

Cursor changes to resize arrows when hovering near window edges. Dragging a tiled window promotes it to floating automatically.

---

## XWayland

Build with `-Dxwayland=enabled` (see Building above) to run X11 applications. X11 windows get the same borders, drop shadows and rounded corners as native windows, can be focused/moved/resized with the mouse, and are included in Super+Tab focus cycling. Close, fullscreen toggle, and move-to-workspace (Super+Shift+c / Super+Shift+f / Super+Shift+1-9, and their `kumde-msg` equivalents) all work on whichever window -- native or X11 -- currently has focus. Maximize and fullscreen requests from the X11 client itself are also honored.

The one deliberate difference: **XWayland windows always float.** They don't participate in the master-stack or monocle tiling layouts, and `Super+t`/`Super+Space` (tile/floating toggle) has no effect on them -- they behave like typical X11 windows floating on top of whatever tiled layout is underneath.

---

## kumbar

Attach to running kumde:
```
kumbar
```

Displays workspace indicators, the focused window's title, and a clock. Workspace indicators update in real time via the IPC socket and are clickable -- left-click a workspace number to switch to it. Redraws every second for clock accuracy.

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

## kumclip

```
kumclip
```

Runs in the background and takes ownership of the Wayland selection whenever a client sets one, keeping the last copied text alive after the source application closes (clipboard persistence). Text-only (`text/plain`, `text/plain;charset=utf-8`, `UTF8_STRING`, `STRING`).

---

## kumidle

```
kumidle --idle-cmd 'kumwall -c 0 0 0' --lock-cmd kumlock --resume-cmd 'kumwall -i ~/wall.png'
```

Watches for seat idle via `ext-idle-notify-v1` and runs commands at three configurable thresholds (`--idle-s 300`, `--lock-s 310`, `--dpms-s 330` by default): an idle command, a lock command (defaults to `kumlock`), and output power off/on via `wlr-randr` at the DPMS threshold. `--resume-cmd` runs once activity resumes after the idle threshold.

---

## kumnotify

```
kumnotify
```

A small notification daemon: listens on a Unix datagram socket (`$XDG_RUNTIME_DIR/kumnotify.sock`) and pops up layer-shell notification bubbles in the top-right corner, stacked and auto-dismissed after a timeout (or on click). Drive it with `kumde-msg notify`:

```
kumde-msg notify myapp "Build finished" "All tests passed" 1 5000
```

---

## kumshot

```
kumshot                      # screenshot the primary output to ~/Pictures/kumshot_TIMESTAMP.png
kumshot -s DP-1               # screenshot a specific output by name
kumshot -a                    # composite all outputs into one image
kumshot -o /path/to/out.png   # explicit output path
```

Captures via `wlr-screencopy-unstable-v1` and writes a PNG.

---

## kumde-msg

Script or bind kumde commands from the shell. `KUMDE_IPC` is exported automatically.

```
kumde-msg workspace 3        # switch to workspace 3
kumde-msg move-to 5          # move focused window to workspace 5
kumde-msg close              # close focused window
kumde-msg layout tile        # set tile layout on focused output
kumde-msg layout float       # set floating layout
kumde-msg layout monocle     # set monocle layout
kumde-msg launch foot        # spawn a command via kumde
kumde-msg reload             # reload config (same as SIGHUP)
kumde-msg quit               # quit kumde
kumde-msg subscribe          # stream IPC events (workspace/occupancy/title/kb_layout) to stdout
kumde-msg notify app "Title" "Body" 1 5000   # send a desktop-style notification to kumnotify
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
│   ├── outputcfg.c      per-output mode/position/scale/transform config
│   ├── toplevel.c       window lifecycle, fullscreen, maximize
│   ├── input.c          keyboard, pointer, edge cursor hints
│   ├── inputcfg.c       libinput device configuration
│   ├── keybind.c        keybind registry and actions
│   ├── border.c         window border decorations
│   ├── shadow.c         drop shadow textures
│   ├── corners.c        rounded corner masks
│   ├── anim.c           animation engine
│   ├── layer.c          layer-shell, usable area
│   ├── workspace.c      per-output workspaces, tiling
│   ├── tiling.c         master-stack resize, swap master
│   ├── focus.c          multi-output focus navigation
│   ├── rules.c          window rules (app_id/title match)
│   ├── conf.c           config file parser
│   ├── kblayout.c       runtime keyboard layout switching
│   ├── autostart.c      autostart command execution
│   ├── ipc.c            Unix socket server, command dispatch
│   └── xwayland.c       XWayland (conditional)
├── kumbar/src/main.c    status bar
├── kumlauncher/src/main.c
├── kumwall/src/main.c
├── kumlock/src/main.c
├── kumclip/src/main.c
├── kumidle/src/main.c
├── kumnotify/src/main.c
├── kumshot/src/main.c
└── tools/kumde-msg.c
```

---

## Protocols

| Protocol | Used by |
|---|---|
| xdg-shell v3 | kumde |
| wlr-layer-shell-unstable-v1 | kumde, kumbar, kumlauncher, kumwall, kumnotify |
| ext-session-lock-v1 | kumlock |
| ext-idle-notify-v1 | kumidle |
| xdg-decoration-unstable-v1 | kumde |
| wlr-screencopy-unstable-v1 | kumde, kumshot |
| wp-viewporter | kumde |
| primary-selection-unstable-v1 | kumde |
| wl_data_device_manager (core) | kumclip |
| xdg-output-unstable-v1 | kumbar |

---

## Packaging

Arch: `packaging/arch/PKGBUILD` (`kumde-git`, no tagged releases exist yet). Build and install locally with `cd packaging/arch && makepkg -si`.

---

## Testing

`meson test -C build` runs the unit tests under `tests/` -- config parsing (including config-file section-scoping edge cases), JSON escaping, and animation timing. These are the parts of the codebase with no wlroots dependency, so they build and run without a Wayland session. The compositor and companion binaries themselves aren't covered by automated tests; verify those by actually running them (`./build/kumde` nested inside an existing session, or from a TTY).

---

## License

MIT. See `LICENSE`.
