# Vendored protocols

`wlr-layer-shell-unstable-v1.xml`, `wlr-screencopy-unstable-v1.xml`, and
`wlr-data-control-unstable-v1.xml` are copied verbatim (MIT-style license
retained in each file's `<copyright>` header) from the
[wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols)
project.

These are wlroots-specific protocol extensions -- unlike `xdg-shell` or
`ext-session-lock-v1`, they were never adopted into the standard
`wayland-protocols` package, so they aren't guaranteed to be available as a
system package on every distribution (Arch ships a separate `wlr-protocols`
package; others may not package it at all). Vendoring them here, rather than
depending on `dependency('wlr-protocols')`, keeps the build portable and is
the same approach most other wlroots-based projects (sway, dwl, etc.) take.

Update by copying a newer revision from upstream if a protocol version bump
is ever needed; there's no automated sync.
