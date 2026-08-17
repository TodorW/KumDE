# Arch packaging

`PKGBUILD` is a `-git` VCS package (no tagged releases exist yet), building
straight from the repository's default branch.

Build and install locally:

```
cd packaging/arch
makepkg -si
```

Before submitting/updating on the AUR, regenerate `.SRCINFO`:

```
makepkg --printsrcinfo > .SRCINFO
```

`check()` runs `meson test`, which covers the wlroots-independent modules
(`tests/`) -- it does not exercise the compositor itself, since that needs a
running Wayland/DRM session.
