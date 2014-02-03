{
  # Shouldn't need this, but doesn't work otherwise.
  # http://crbug.com/340086
  'target_defaults': {
    'variables': {
      'deps': [
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libcrash',
      'type': 'static_library',
      'variables': {
        'exported_deps': [
          'glib-2.0',
          'gobject-2.0',
          'libchrome-<(libbase_ver)',
          'libpcrecpp',
        ],
        'deps': ['<@(exported_deps)'],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
        'link_settings': {
          'libraries': [
            '-lgflags',
          ],
        },
      },
      'dependencies': [
        '../libchromeos/libchromeos-<(libbase_ver).gyp:libchromeos-<(libbase_ver)',
      ],
      'sources': [
        'chrome_collector.cc',
        'crash_collector.cc',
        'kernel_collector.cc',
        'kernel_warning_collector.cc',
        'udev_collector.cc',
        'unclean_shutdown_collector.cc',
        'user_collector.cc',
      ],
    },
    {
      'target_name': 'crash_reporter',
      'type': 'executable',
      'variables': {
        'deps': [
          'dbus-1',
          'dbus-glib-1',
        ],
      },
      'dependencies': [
        'libcrash',
        '../libchromeos/libchromeos-<(libbase_ver).gyp:libchromeos-<(libbase_ver)',
        '../metrics/metrics.gyp:libmetrics',
      ],
      'sources': [
        'crash_reporter.cc',
      ],
    },
    {
      'target_name': 'list_proxies',
      'type': 'executable',
      'variables': {
        'deps': [
          'dbus-1',
          'dbus-glib-1',
          'libchrome-<(libbase_ver)',
        ],
      },
      'link_settings': {
        'libraries': [
          '-lgflags',
        ],
      },
      'dependencies': [
        '../libchromeos/libchromeos-<(libbase_ver).gyp:libchromeos-<(libbase_ver)',
      ],
      'sources': [
        'list_proxies.cc',
      ],
    },
    {
      'target_name': 'warn_collector',
      'type': 'executable',
      'variables': {
        'lexer_out_dir': 'crash-reporter',
      },
      'link_settings': {
        'libraries': [
          '-lfl',
        ],
      },
      'dependencies': [
        '../metrics/metrics.gyp:libmetrics',
      ],
      'sources': [
        'warn_collector.l',
      ],
      'includes': ['../common-mk/lex.gypi'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'chrome_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'chrome_collector_test.cc',
          ]
        },
        {
          'target_name': 'crash_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'crash_collector_test.cc',
          ]
        },
        {
          'target_name': 'kernel_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'kernel_collector_test.cc',
          ]
        },
        {
          'target_name': 'udev_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'udev_collector_test.cc',
          ]
        },
        {
          'target_name': 'unclean_shutdown_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'unclean_shutdown_collector_test.cc',
          ]
        },
        {
          'target_name': 'user_collector_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'user_collector_test.cc',
          ]
        },
      ],
    }],
  ],
}
