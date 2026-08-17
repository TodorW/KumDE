project('kumde', 'c',
  version : '0.8.0',
  default_options : ['c_std=c11', 'warning_level=2'],
)

cc = meson.get_compiler('c')

wlroots = disabler()
foreach v : ['0.20', '0.19', '0.18', '0.17']
  if not wlroots.found()
    wlroots = dependency('wlroots-@0@'.format(v), required : false)
  endif
endforeach
if not wlroots.found()
  error('wlroots 0.17 through 0.20 not found (tried wlroots-0.17 .. wlroots-0.20)')
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
  '-D_GNU_SOURCE',
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
  'src/pixelbuffer.c',
  'src/layer.c',
  'src/sessionlock.c',
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

# wlroots' installed headers reference this generated server-side
# protocol header directly (#include "wlr-layer-shell-unstable-v1-protocol.h")
# rather than shipping a pre-generated copy themselves.
wayland_scanner = find_program('wayland-scanner')
layer_shell_server_header = custom_target('wlr-layer-shell-unstable-v1-protocol.h',
  input   : 'protocols/wlr-layer-shell-unstable-v1.xml',
  output  : 'wlr-layer-shell-unstable-v1-protocol.h',
  command : [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
sources += [layer_shell_server_header]

kumde_exe = executable('kumde',
  sources,
  include_directories : [include_directories('include'), include_directories('.')],
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
