cc = meson.get_compiler('c')

wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
cairo           = dependency('cairo')
xkbcommon       = dependency('xkbcommon')
libpam          = cc.find_library('pam')
libm            = cc.find_library('m')
librt           = cc.find_library('rt')


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
  include_directories : include_directories('.'),
  dependencies : [wayland_client, cairo, xkbcommon, libpam, libm, librt],
  install : true,
)
