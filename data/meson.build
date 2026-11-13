name_conf = configuration_data()
name_conf.set('NM_DBUS_SERVICE_ZEROTIER', 'org.freedesktop.NetworkManager.zerotier')
name_conf.set('LIBEXECDIR', libexecdir)

# The [libnm] editor plugin and [GNOME] auth-dialog only exist in GTK
# builds; omit those sections otherwise so the .name file doesn't
# reference files that aren't installed.
libnm_section = ''
gnome_section = ''
if build_gtk4
  libnm_section = '\n[libnm]\nplugin=libnm-vpn-plugin-zerotier.so\n'
  gnome_section = '\n[GNOME]\nauth-dialog=@0@/nm-zerotier-auth-dialog\nsupports-external-ui-mode=true\nsupports-hints=true'.format(libexecdir)
endif
name_conf.set('LIBNM_SECTION', libnm_section)
name_conf.set('GNOME_SECTION', gnome_section)

configure_file(
  input: 'nm-zerotier-service.name.in',
  output: 'nm-zerotier-service.name',
  configuration: name_conf,
  install: true,
  install_dir: vpnservicedir,
)

install_data(
  'nm-zerotier-service.conf',
  install_dir: '/usr/share/dbus-1/system.d',
)

install_data(
  'nm-zerotier-status.conf',
  install_dir: '/usr/share/dbus-1/system.d',
)

systemd_dep = dependency('systemd')
systemd_unit_dir = systemd_dep.get_variable(pkgconfig: 'systemdsystemunitdir')

service_conf = configuration_data()
service_conf.set('LIBEXECDIR', libexecdir)

configure_file(
  input: 'nm-zerotier-reconciled.service.in',
  output: 'nm-zerotier-reconciled.service',
  configuration: service_conf,
  install: true,
  install_dir: systemd_unit_dir,
)
