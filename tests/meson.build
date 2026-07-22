test_zerotier_cli_wrapper = executable(
  'test-zerotier-cli-wrapper',
  [
    'test-zerotier-cli-wrapper.c',
    '../src/zerotier-cli-wrapper.c',
    '../src/zerotier-network-id.c',
  ],
  include_directories: include_directories('../src'),
  dependencies: [glib_dep, gio_dep, json_glib_dep],
)

test('zerotier-cli-wrapper', test_zerotier_cli_wrapper)

test_zerotier_reconcile_diff = executable(
  'test-zerotier-reconcile-diff',
  [
    'test-zerotier-reconcile-diff.c',
    '../reconciled/nm-zerotier-reconcile-diff.c',
  ],
  include_directories: include_directories('../reconciled'),
  dependencies: [glib_dep],
)

test('zerotier-reconcile-diff', test_zerotier_reconcile_diff)

test_nm_zerotier_status_dbus = executable(
  'test-nm-zerotier-status-dbus',
  [
    'test-nm-zerotier-status-dbus.c',
    '../reconciled/nm-zerotier-status-dbus.c',
    '../src/zerotier-cli-wrapper.c',
    '../src/zerotier-network-id.c',
  ],
  include_directories: include_directories('../src', '../reconciled'),
  dependencies: [glib_dep, gio_dep, json_glib_dep],
)

test('nm-zerotier-status-dbus', test_nm_zerotier_status_dbus)
