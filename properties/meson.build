gnome = import('gnome')
gmodule_dep = dependency('gmodule-2.0')

zerotier_resources = gnome.compile_resources(
  'zerotier-resources',
  'gresource.xml',
  c_name: 'zerotier_resources',
)

shared_module(
  'nm-vpn-plugin-zerotier',
  [
    'nm-zerotier-editor-plugin.c',
    'nm-zerotier-editor.c',
    zerotier_resources,
    '../src/zerotier-network-id.c',
  ],
  include_directories: include_directories('../src'),
  dependencies: [libnm_dep, gtk4_dep, gmodule_dep, glib_dep, gio_dep],
  install: true,
  install_dir: nm_plugin_dir,
)
