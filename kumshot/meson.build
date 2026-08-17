cc = meson.get_compiler('c')
wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
cairo           = dependency('cairo')
librt           = cc.find_library('rt')
scanner_code   = generator(wayland_scanner,
    output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner,
    output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])
wl_proto_dir = wayland_proto.get_variable('pkgdatadir')
screencopy_xml = wl_proto_dir / 'unstable/wlr-screencopy/wlr-screencopy-unstable-v1.xml'
layer_xml = wl_proto_dir / 'staging/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml'
if not import('fs').exists(layer_xml)
  layer_xml = wl_proto_dir / 'unstable/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml'
endif
protocols = [screencopy_xml, layer_xml]
proto_src = []
proto_hdr = []
foreach p : protocols
  proto_src += scanner_code.process(p)
  proto_hdr += scanner_header.process(p)
endforeach
executable('kumshot',
  sources : ['src/main.c', proto_src, proto_hdr],
  include_directories : include_directories(meson.current_build_dir()),
  dependencies : [wayland_client, cairo, librt], install : true)
