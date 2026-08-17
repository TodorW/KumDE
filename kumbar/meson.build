cc = meson.get_compiler('c')

wayland_client   = dependency('wayland-client')
wayland_scanner  = find_program('wayland-scanner')
wayland_proto    = dependency('wayland-protocols')
cairo            = dependency('cairo')
libm             = cc.find_library('m')
librt            = cc.find_library('rt')

wayland_scanner_code = generator(
  wayland_scanner,
  output  : '@BASENAME@-client-protocol.c',
  arguments : ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_header = generator(
  wayland_scanner,
  output  : '@BASENAME@-client-protocol.h',
  arguments : ['client-header', '@INPUT@', '@OUTPUT@'],
)

wl_proto_dir  = wayland_proto.get_variable('pkgdatadir')
wlr_proto_dir = dependency('wlr-protocols').get_variable('pkgdatadir')

# wlr-layer-shell's get_popup request references xdg_popup, so the
# generated code needs xdg-shell's interface table linked in too, even
# though this client never uses xdg-shell itself.
xdg_shell_xml   = wl_proto_dir / 'stable/xdg-shell/xdg-shell.xml'
layer_shell_xml = wlr_proto_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'
xdg_output_xml  = wl_proto_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'

protocols = [xdg_shell_xml, layer_shell_xml, xdg_output_xml]
proto_src  = []
proto_hdr  = []

foreach p : protocols
  proto_src += wayland_scanner_code.process(p)
  proto_hdr += wayland_scanner_header.process(p)
endforeach

executable('kumbar',
  sources : [
    'src/main.c',
    proto_src,
    proto_hdr,
  ],
  include_directories : [
    include_directories('include'),
    include_directories('.'),
  ],
  dependencies : [wayland_client, cairo, libm, librt],
  install : true,
)
