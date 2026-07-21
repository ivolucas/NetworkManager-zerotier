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
