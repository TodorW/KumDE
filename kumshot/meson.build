cc = meson.get_compiler('c')
wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
cairo           = dependency('cairo')
xkbcommon       = dependency('xkbcommon')
librt           = cc.find_library('rt')
scanner_code   = generator(wayland_scanner,
    output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner,
    output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])
# wlr-layer-shell's get_popup request references xdg_popup, so xdg-shell's
# interface table must be linked in too, even though it's otherwise unused.
# wlr-screencopy and wlr-layer-shell are vendored under ../protocols (see
# protocols/README.md).
xdg_shell_xml = wayland_proto.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
screencopy_xml = '../protocols/wlr-screencopy-unstable-v1.xml'
layer_xml = '../protocols/wlr-layer-shell-unstable-v1.xml'
protocols = [xdg_shell_xml, screencopy_xml, layer_xml]
proto_src = []
proto_hdr = []
foreach p : protocols
  proto_src += scanner_code.process(p)
  proto_hdr += scanner_header.process(p)
endforeach
executable('kumshot',
  sources : ['src/main.c', proto_src, proto_hdr],
  include_directories : include_directories('.'),
  dependencies : [wayland_client, cairo, xkbcommon, librt], install : true)
