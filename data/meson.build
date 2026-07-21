name_conf = configuration_data()
name_conf.set('NM_DBUS_SERVICE_ZEROTIER', 'org.freedesktop.NetworkManager.zerotier')
name_conf.set('LIBEXECDIR', libexecdir)

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
