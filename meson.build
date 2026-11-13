project('NetworkManager-zerotier', 'c',
  version: '1.0.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=gnu11', 'warning_level=2'],
)

libnm_dep = dependency('libnm')
gio_dep = dependency('gio-2.0')
glib_dep = dependency('glib-2.0')
json_glib_dep = dependency('json-glib-1.0')

vpnservicedir = libnm_dep.get_variable(pkgconfig: 'vpnservicedir', default_value: '/usr/lib/NetworkManager/VPN')
# Some distros (e.g. Fedora) don't define libnm's "plugindir" pkg-config
# variable; fall back to <prefix>/<libdir>/NetworkManager so editor plugins
# land in the arch-specific directory (/usr/lib64/NetworkManager) where
# NetworkManager and the connection editors look for them.
nm_plugin_dir = libnm_dep.get_variable(pkgconfig: 'plugindir',
                                       default_value: join_paths(get_option('prefix'), get_option('libdir'), 'NetworkManager'))

prefix = get_option('prefix')
libexecdir = join_paths(prefix, get_option('libexecdir'))

build_gtk4 = get_option('gtk4')
if build_gtk4
  gtk4_dep = dependency('gtk4')
endif

subdir('src')
subdir('reconciled')
subdir('auth-dialog')
subdir('data')
if build_gtk4
  subdir('properties')
endif
subdir('tests')
