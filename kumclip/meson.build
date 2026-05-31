project('kumclip', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=2'],
)
wayland_client = dependency('wayland-client')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language : 'c')
executable('kumclip', sources : ['src/main.c'],
  dependencies : [wayland_client], install : true)
