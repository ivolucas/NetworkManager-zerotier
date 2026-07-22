executable(
  'nm-zerotier-reconciled',
  [
    'nm-zerotier-reconciled.c',
    'nm-zerotier-reconciler.c',
    'nm-zerotier-reconcile-diff.c',
    'nm-zerotier-status-dbus.c',
    '../src/zerotier-cli-wrapper.c',
    '../src/zerotier-network-id.c',
  ],
  include_directories: include_directories('../src'),
  dependencies: [libnm_dep, gio_dep, glib_dep, json_glib_dep],
  install: true,
  install_dir: libexecdir,
)
