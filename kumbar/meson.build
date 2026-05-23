project('kumbar', 'c',
  version : '0.1.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

wayland_client   = dependency('wayland-client')
wayland_scanner  = find_program('wayland-scanner')
wayland_proto    = dependency('wayland-protocols')
cairo            = dependency('cairo')
libm             = cc.find_library('m')
librt            = cc.find_library('rt')

add_project_arguments(
  '-D_POSIX_C_SOURCE=200809L',
  language : 'c',
)

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

wl_proto_dir = wayland_proto.get_variable('pkgdatadir')

layer_shell_xml = wl_proto_dir / 'staging/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml'
if not import('fs').exists(layer_shell_xml)
  layer_shell_xml = wl_proto_dir / 'unstable/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml'
endif

xdg_output_xml = wl_proto_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'

protocols = [layer_shell_xml, xdg_output_xml]
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
    include_directories(meson.current_build_dir()),
  ],
  dependencies : [wayland_client, cairo, libm, librt],
  install : true,
)
