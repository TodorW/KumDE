cc = meson.get_compiler('c')
wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
cairo           = dependency('cairo')
libm            = cc.find_library('m')
librt           = cc.find_library('rt')
scanner_code   = generator(wayland_scanner, output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner, output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])
wlr_proto_dir = dependency('wlr-protocols').get_variable('pkgdatadir')
# wlr-layer-shell's get_popup request references xdg_popup, so xdg-shell's
# interface table must be linked in too, even though it's otherwise unused.
xdg_shell_xml = wayland_proto.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
layer_xml = wlr_proto_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'
executable('kumnotify',
  sources : ['src/main.c',
    scanner_code.process(xdg_shell_xml), scanner_header.process(xdg_shell_xml),
    scanner_code.process(layer_xml), scanner_header.process(layer_xml)],
  include_directories : include_directories('.'),
  dependencies : [wayland_client, cairo, libm, librt], install : true)
