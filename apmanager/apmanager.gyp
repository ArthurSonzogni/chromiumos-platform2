{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
      ],
    },
    'cflags': [
      '-Wextra',
      '-Wno-unused-parameter',  # base/lazy_instance.h, etc.
    ],
    'cflags_cc': [
      '-Wno-missing-field-initializers', # for LAZY_INSTANCE_INITIALIZER
    ],
  },
  'targets': [
    {
      'target_name': 'apmanager-adaptors',
      'type': 'none',
      'variables': {
        'dbus_adaptors_out_dir': 'include/apmanager/dbus_adaptors',
      },
      'sources': [
        'dbus_bindings/org.chromium.apmanager.Config.xml',
        'dbus_bindings/org.chromium.apmanager.Device.xml',
        'dbus_bindings/org.chromium.apmanager.Manager.xml',
        'dbus_bindings/org.chromium.apmanager.Service.xml',
      ],
      'includes': ['../common-mk/generate-dbus-adaptors.gypi'],
    },
    {
      'target_name': 'libapmanager',
      'type': 'static_library',
      'dependencies': [
        'apmanager-adaptors',
      ],
      'variables': {
        'exported_deps': [
          'libshill-net-<(libbase_ver)',
        ],
        'deps': ['<@(exported_deps)'],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'sources': [
        'config.cc',
        'daemon.cc',
        'device.cc',
        'device_info.cc',
        'dhcp_server.cc',
        'dhcp_server_factory.cc',
        'event_dispatcher.cc',
        'file_writer.cc',
        'firewall_manager.cc',
        'hostapd_monitor.cc',
        'manager.cc',
        'process_factory.cc',
        'service.cc',
        'shill_proxy.cc',
      ],
      'actions': [
        {
          'action_name': 'generate-shill-proxies',
          'variables': {
            'proxy_output_file': 'include/shill/dbus-proxies.h'
          },
          'sources': [
            '../shill/dbus_bindings/org.chromium.flimflam.Manager.xml',
          ],
          'includes': ['../common-mk/generate-dbus-proxies.gypi'],
        },
        {
          'action_name': 'generate-permission_broker-proxies',
          'variables': {
            'proxy_output_file': 'include/permission_broker/dbus-proxies.h'
          },
          'sources': [
            '../permission_broker/dbus_bindings/org.chromium.PermissionBroker.xml',
          ],
          'includes': ['../common-mk/generate-dbus-proxies.gypi'],
        },
      ],
    },
    {
      'target_name': 'apmanager',
      'type': 'executable',
      'dependencies': ['libapmanager'],
      'link_settings': {
        'libraries': [
          '-lminijail',
        ],
      },
      'sources': [
        'main.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'apmanager_testrunner',
          'type': 'executable',
          'dependencies': ['libapmanager'],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'config_unittest.cc',
            'device_info_unittest.cc',
            'device_unittest.cc',
            'dhcp_server_unittest.cc',
            'hostapd_monitor_unittest.cc',
            'manager_unittest.cc',
            'mock_config.cc',
            'mock_device.cc',
            'mock_dhcp_server.cc',
            'mock_dhcp_server_factory.cc',
            'mock_event_dispatcher.cc',
            'mock_file_writer.cc',
            'mock_hostapd_monitor.cc',
            'mock_manager.cc',
            'mock_process_factory.cc',
            'mock_service.cc',
            'service_unittest.cc',
            'testrunner.cc',
          ],
        },
      ],
    }],
  ],
}
