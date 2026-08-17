# Debian packaging

`control`/`rules`/`changelog`/`copyright`/`source/format` build a native
Debian source package straight from the repository's default branch (no
tagged releases exist yet), mirroring `packaging/arch`'s VCS-package
approach.

Debian tooling expects a `debian/` directory at the repository root, not
nested under `packaging/`, so symlink it there first:

```
ln -s packaging/debian debian
```

Build and install locally:

```
sudo apt-get build-dep .
dpkg-buildpackage -us -uc -b
sudo apt-get install ../kumde_*.deb
```

Remove the symlink again afterwards (`rm debian`) -- it's a local build
convenience, not committed to the repo, so `packaging/debian/` stays the
single source of truth alongside `packaging/arch/`.

`libwlroots-0.20-dev` (Debian sid, as of packaging) is the wlroots version
this was built and tested against; `debian/rules` builds with
`-Dxwayland=enabled` explicitly since Debian's default meson auto-detection
otherwise depends on whichever XCB dev packages happen to be pulled in as
transitive build-deps.

Verified 2026-08-18 in a clean debootstrapped Debian sid chroot
(systemd-nspawn): `dpkg-buildpackage -us -uc -b` builds cleanly, `meson
test` (run via `dh_auto_test`) passes all 3 unit tests, the resulting
`.deb` installs with `apt-get install` (dependencies resolve from Debian
sid's repos, no unmet deps), and `kumde -v` runs correctly post-install.
Not yet tested on Debian stable/testing -- sid was used because it's the
only Debian release currently shipping `libwlroots-0.20-dev`; older
releases would need `libwlroots-0.19-dev` and a corresponding
`Build-Depends` change (kumde's own meson.build already accepts wlroots
0.17 through 0.20, so that's just a packaging-side edit, not a code one).
