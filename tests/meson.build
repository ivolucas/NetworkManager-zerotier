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
