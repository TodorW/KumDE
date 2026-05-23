project('kumde', 'c',
  version : '0.5.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

wlroots = dependency('wlroots-0.18', required : false)
if not wlroots.found()
  wlroots = dependency('wlroots-0.17')
endif

wayland_server = dependency('wayland-server')
xkbcommon      = dependency('xkbcommon')
libm           = cc.find_library('m')

wlr_version = wlroots.version().split('.')
wlr_minor   = wlr_version[1].to_int()

add_project_arguments(
  '-DWLR_VERSION_MINOR=@0@'.format(wlr_minor),
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  language : 'c',
)

sources = files(
  'src/main.c',
  'src/server.c',
  'src/output.c',
  'src/toplevel.c',
  'src/input.c',
  'src/keybind.c',
  'src/border.c',
  'src/anim.c',
  'src/layer.c',
  'src/workspace.c',
  'src/conf.c',
  'src/shadow.c',
  'src/ipc.c',
)

xwayland_dep = dependency('xcb', required : get_option('xwayland'))
xwayland     = get_option('xwayland').enabled() or \
               (get_option('xwayland').auto() and xwayland_dep.found())

if xwayland
  sources += files('src/xwayland.c')
  add_project_arguments('-DKUM_XWAYLAND', language : 'c')
endif

executable('kumde',
  sources,
  include_directories : include_directories('include'),
  dependencies : [wlroots, wayland_server, xkbcommon, libm],
  install : true,
)

subdir('kumbar')
subdir('kumlauncher')
subdir('kumwall')
subdir('kumlock')
subdir('tools')

summary({
  'version'    : meson.project_version(),
  'wlroots'    : wlroots.version(),
  'xwayland'   : xwayland,
  'workspaces' : 9,
}, section : 'kumde')
