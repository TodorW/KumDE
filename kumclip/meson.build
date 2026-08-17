wayland_client = dependency('wayland-client')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language : 'c')
executable('kumclip', sources : ['src/main.c'],
  dependencies : [wayland_client], install : true)
