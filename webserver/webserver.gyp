{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
      ],
    },
    'include_dirs': ['.'],
  },
  'targets': [
    {
      'target_name': 'libwebserv-<(libbase_ver)',
      'type': 'shared_library',
      'variables': {
        'exported_deps': [
          'libmicrohttpd',
        ],
        'deps': ['<@(exported_deps)'],
        'dbus_adaptors_out_dir': 'include/webservd',
        'dbus_service_config': 'webservd/dbus_bindings/dbus-service-config.json',
      },
      'includes': ['../common-mk/deps.gypi'],
      'sources': [
        'libwebserv/connection.cc',
        'libwebserv/response.cc',
        'libwebserv/request.cc',
        'libwebserv/request_handler_callback.cc',
        'libwebserv/server.cc',
      ],
      'actions': [
        {
          'action_name': 'generate-webservd-proxies',
          'variables': {
            'dbus_service_config': 'webservd/dbus_bindings/dbus-service-config.json',
            'mock_output_file': 'include/webservd/dbus-mocks.h',
            'proxy_output_file': 'include/webservd/dbus-proxies.h',
          },
          'sources': [
            'webservd/dbus_bindings/org.chromium.WebServer.Manager.xml',
            'webservd/dbus_bindings/org.chromium.WebServer.Server.xml',
          ],
          'includes': ['../common-mk/generate-dbus-proxies.gypi'],
        },
      ],
    },
    {
      'target_name': 'webservd',
      'type': 'executable',
      'variables': {
        'exported_deps': [
          'libmicrohttpd',
        ],
        'deps': ['<@(exported_deps)'],
        'dbus_adaptors_out_dir': 'include/webservd',
        'dbus_service_config': 'webservd/dbus_bindings/dbus-service-config.json',
      },
      'sources': [
        'webservd/dbus_bindings/org.chromium.WebServer.Manager.xml',
        'webservd/dbus_bindings/org.chromium.WebServer.Server.xml',
        'webservd/main.cc',
        'webservd/manager.cc',
      ],
      'includes': ['../common-mk/generate-dbus-adaptors.gypi'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'libwebserv_testrunner',
          'type': 'executable',
          'dependencies': [
            'libwebserv-<(libbase_ver)',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'libwebserv/libwebserv_testrunner.cc',
          ],
        },
      ],
    }],
  ],
}
