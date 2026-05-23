project('kumlock', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=2'],
)

cc = meson.get_compiler('c')

wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
cairo           = dependency('cairo')
xkbcommon       = dependency('xkbcommon')
libm            = cc.find_library('m')
librt           = cc.find_library('rt')

add_project_arguments('-D_POSIX_C_SOURCE=200809L', language : 'c')

scanner_code   = generator(wayland_scanner, output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner, output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])

wl_proto_dir = wayland_proto.get_variable('pkgdatadir')
lock_xml = wl_proto_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'

proto_src = [scanner_code.process(lock_xml)]
proto_hdr = [scanner_header.process(lock_xml)]

executable('kumlock',
  sources : ['src/main.c', proto_src, proto_hdr],
  include_directories : include_directories(meson.current_build_dir()),
  dependencies : [wayland_client, cairo, xkbcommon, libm, librt],
  install : true,
)
