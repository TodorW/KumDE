wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
wayland_proto   = dependency('wayland-protocols')
scanner_code   = generator(wayland_scanner, output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner, output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])
idle_xml = wayland_proto.get_variable('pkgdatadir') / 'staging/ext-idle-notify/ext-idle-notify-v1.xml'
executable('kumidle',
  sources : ['src/main.c', scanner_code.process(idle_xml), scanner_header.process(idle_xml)],
  include_directories : include_directories(meson.current_build_dir()),
  dependencies : [wayland_client], install : true)
