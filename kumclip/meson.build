wayland_client  = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
scanner_code   = generator(wayland_scanner, output : '@BASENAME@-client-protocol.c',
    arguments : ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_header = generator(wayland_scanner, output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@'])
# wlr-data-control is vendored under ../protocols (see protocols/README.md).
data_control_xml = '../protocols/wlr-data-control-unstable-v1.xml'
executable('kumclip',
  sources : ['src/main.c',
    scanner_code.process(data_control_xml),
    scanner_header.process(data_control_xml)],
  include_directories : include_directories('.'),
  dependencies : [wayland_client], install : true)
