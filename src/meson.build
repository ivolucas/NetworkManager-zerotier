gio_unix_dep = dependency('gio-unix-2.0')

executable(
  'nm-zerotier-service',
  [
    'nm-zerotier-service.c',
    'nm-zerotier-plugin.c',
    'zerotier-cli-wrapper.c',
    'zerotier-network-id.c',
  ],
  dependencies: [libnm_dep, gio_dep, gio_unix_dep, glib_dep, json_glib_dep],
  install: true,
  install_dir: libexecdir,
)
