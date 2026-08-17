# Contributing to KumDE

Thanks for taking a look. This file is aimed at someone who wants to build,
test, and change the code -- for user-facing docs (config, keybindings,
companion binaries) see `README.md`.

## Building

See `README.md`'s Building/Requirements sections. Short version:

```
meson setup build -Dxwayland=enabled
ninja -C build
meson test -C build
```

## Testing philosophy: run it, don't just read it

This is the single most important thing to know before changing anything
here. **Most real bugs in this codebase have been invisible to code review**
and only surfaced by actually running the compositor and looking at what it
produced. The worst historical example: kumde never sent the first
`configure` event to any surface, so *nothing ever rendered* -- zero
compile warnings, zero crashes, a process that stayed alive indefinitely.
The only way anyone caught it was noticing a screenshot was pixel-exact
black. Several companion binaries have shipped using the wrong protocol
entirely (working, mapped, no crash -- just silently doing nothing), which
is also invisible from the source alone.

So: before believing a change works, run it and look.

**Fastest loop -- headless, no display needed at all:**

```
WLR_BACKENDS=headless WLR_RENDERER=pixman ./build/kumde &
WAYLAND_DISPLAY=wayland-0 ./build/kumshot/kumshot -o /tmp/out.png
# open /tmp/out.png and actually look at it
```

wlroots' headless backend + the pixman software renderer need no GPU and no
host display server -- this boots the real compositor (IPC, XWayland
included) in a plain shell. Check kumde's own startup log for which
`wayland-N` socket it actually picked (`wl_display_add_socket_auto` chooses
it, not whatever `WAYLAND_DISPLAY` you had set beforehand). `tests/
smoke_headless.sh` is the automated version of this exact check, wired into
`meson test` as a 4th test -- it fails if the screenshot is suspiciously
small (a solid-color "nothing rendered" PNG compresses to a few hundred
bytes; a real rendered desktop is double-digit KB), which is enough to
catch a repeat of the never-renders bug without a human watching.

**For anything needing real interaction (mouse clicks/drags, typing):**
run kumde nested inside an existing Wayland session
(`WAYLAND_DISPLAY=wayland-1 ./build/kumde` from inside your normal desktop)
and drive it with a synthetic input tool (`ydotool` worked well). If you're
testing inside a *nested* session under another compositor, be aware that
host-level modifier-key shortcuts (Super/Meta especially) can get
intercepted by the host compositor before your keystrokes ever reach the
nested kumde -- if a mouse/keyboard interaction seems to do nothing, check
whether the host window manager did something instead (moved its own
window, opened its own menu) before assuming kumde is broken.

**For crashes:** `coredumpctl list` / `coredumpctl debug <pid> --no-pager
-A "-batch -ex bt"`.

**Temporary `wlr_log(WLR_ERROR, "DEBUG ...")` lines** are a legitimate and
fast way to disambiguate competing hypotheses about *why* something
silently isn't working -- just remove them before committing.

## Known gaps / places to start

These aren't hidden -- they're the honest state of things as of the last
pass through the codebase:

- **Super+drag window move was never empirically verified.** It's wired up
  (`kum_toplevel_begin_interactive`/`kum_xwayland_begin_interactive` in
  `src/input.c`, `src/toplevel.c`, `src/xwayland.c`) and uses the same grab
  machinery that border-click resize uses (which *is* verified working),
  but every attempt to test it hit the host-Meta-key-interception problem
  described above. If you can test this on bare metal or in an environment
  without a host compositor swallowing Super, it's worth confirming
  directly.
- **Output hotplug removal is untested.** Multiple outputs present at boot
  works (`WLR_WL_OUTPUTS=2 ./build/kumde` for a quick nested test), but an
  output actually *disconnecting* at runtime hasn't been exercised.
- **Test coverage is thin.** `tests/` covers config parsing, JSON escaping,
  animation-easing math, and one headless boot+render smoke test. Nothing
  exercises tiling layout math, layer-shell placement, session-lock,
  multi-workspace/multi-output interaction, or any companion binary, under
  automated test. Given how much this project has been bitten by things
  that only break interactively, more coverage here -- even simple
  screenshot-diff-style checks driven off the headless backend -- would be
  high value.
- **Packaging exists for Arch and Debian only** (`packaging/arch`,
  `packaging/debian`). The Debian package specifically targets sid because
  that's the only release currently shipping `libwlroots-0.20-dev`; a port
  to an older Debian/Ubuntu release, or a Fedora/openSUSE package, would
  need to either patch around an older wlroots or wait for backports.
- Beyond those specific items: given the project's actual track record,
  assume any interactive path (mouse-driven or otherwise) that nobody has
  specifically screenshotted or driven with synthetic input recently might
  have a silent bug. Testing an existing feature interactively and finding
  nothing wrong is still a useful contribution -- it turns an assumption
  into a verified fact.

## Code conventions

Nothing formal, but consistency matters:

- 4-space indentation, no tabs.
- Comments explain *why*, not *what* -- the code should read clearly enough
  that a comment restating it would be redundant. Reserve comments for
  non-obvious constraints, workarounds, or invariants (see the top of
  `src/toplevel.c` for an example).
- New wlroots-independent logic (parsing, pure math, string handling)
  should get a unit test under `tests/` alongside it -- that's the part of
  the codebase that actually *can* be tested cheaply and deterministically,
  so there's no excuse not to.
- Keep commits granular and self-contained -- one fix or one feature per
  commit, with a message that explains the root cause and, ideally, how you
  verified the fix (not just what changed). Look at recent `git log` output
  for the expected level of detail.

## Where things live

See `README.md`'s "Source layout" section for the file-by-file map of
`src/` and the companion binaries.
