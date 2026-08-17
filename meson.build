project('kumde', 'c',
  version : '0.8.0',
  default_options : ['c_std=c11', 'warning_level=2'],
)

cc = meson.get_compiler('c')

wlroots = dependency('wlroots-0.18', required : false)
if not wlroots.found()
  wlroots = dependency('wlroots-0.17')
endif

wayland_server = dependency('wayland-server')
xkbcommon      = dependency('xkbcommon')
libinput       = dependency('libinput')
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
  'src/easing.c',
  'src/jsonutil.c',
  'src/layer.c',
  'src/workspace.c',
  'src/conf.c',
  'src/shadow.c',
  'src/ipc.c',
  'src/corners.c',
  'src/rules.c',
  'src/focus.c',
  'src/inputcfg.c',
  'src/outputcfg.c',
  'src/autostart.c',
  'src/kblayout.c',
  'src/tiling.c',
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
  dependencies : [wlroots, wayland_server, xkbcommon, libinput, libm],
  install : true,
)

install_man('contrib/man/kumde.1')
install_man('contrib/man/kumde-msg.1')
install_man('contrib/man/kumshot.1')
install_man('contrib/man/kumidle.1')
install_man('contrib/man/kumbar.1')
install_man('contrib/man/kumlauncher.1')
install_man('contrib/man/kumwall.1')
install_man('contrib/man/kumlock.1')
install_man('contrib/man/kumclip.1')
install_man('contrib/man/kumnotify.1')

install_data('contrib/kumde.conf',
  install_dir : get_option('datadir') / 'kumde')

install_data(
  'contrib/desktop/kumlauncher.desktop',
  'contrib/desktop/kumshot.desktop',
  'contrib/desktop/kumlock.desktop',
  'contrib/desktop/kumbar.desktop',
  'contrib/desktop/kumclip.desktop',
  'contrib/desktop/kumidle.desktop',
  'contrib/desktop/kumnotify.desktop',
  install_dir : get_option('datadir') / 'applications')

install_data('contrib/desktop/kumde.desktop',
  install_dir : get_option('datadir') / 'wayland-sessions')

subdir('kumbar')
subdir('kumlauncher')
subdir('kumwall')
subdir('kumlock')
subdir('kumclip')
subdir('kumidle')
subdir('kumnotify')
subdir('kumshot')
subdir('tools')
subdir('tests')

summary({
  'version'    : meson.project_version(),
  'wlroots'    : wlroots.version(),
  'xwayland'   : xwayland,
  'workspaces' : 9,
}, section : 'kumde')
