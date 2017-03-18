{
  'target_defaults': {
    'variables': {
      'deps': [
        'dbus-c++-1',
        'protobuf-lite',
        'openssl',
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
      ],
      # Chaps uses try/catch to interact with dbus-c++.
      'enable_exceptions': 1,
    },
    'defines': [
      'USE_TPM2=<(USE_tpm2)',
    ],
  },
  'targets': [
    {
      'target_name': 'chaps-protos',
      'type': 'static_library',
      # libchaps-protos.a is used by a shared_libary
      # object, so we need to build it with '-fPIC' instead of '-fPIE'.
      'cflags!': ['-fPIE'],
      'cflags': ['-fPIC'],
      'variables': {
        'proto_in_dir': '.',
        'proto_out_dir': 'include/chaps/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/attributes.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    {
      'target_name': 'chaps-proxies',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'proxy',
        'xml2cpp_in_dir': '.',
        'xml2cpp_out_dir': 'include/chaps/dbus_proxies',
      },
      'sources': [
        '<(xml2cpp_in_dir)/chaps_interface.xml',
      ],
      'includes': ['../common-mk/xml2cpp.gypi'],
    },
    {
      'target_name': 'chaps-adaptors',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'adaptor',
        'xml2cpp_in_dir': '.',
        'xml2cpp_out_dir': 'include/chaps/dbus_adaptors',
      },
      'sources': [
        '<(xml2cpp_in_dir)/chaps_interface.xml',
      ],
      'includes': ['../common-mk/xml2cpp.gypi'],
    },
    {
      # This is meant only for internal binaries like unittests.  All
      # client programs should be using the shared libchaps library.
      'target_name': 'libchaps_static',
      'type': 'static_library',
      'dependencies': [
        'chaps-adaptors',
        'chaps-protos',
        'chaps-proxies',
      ],
      'sources': [
        'attributes.cc',
        'chaps.cc',
        'chaps_proxy.cc',
        'chaps_utility.cc',
        'isolate_chromeos.cc',
        'token_manager_client.cc',
      ],
    },
    {
      'target_name': 'libchaps',
      'type': 'shared_library',
      'dependencies': [
        'chaps-adaptors',
        'chaps-protos',
        'chaps-proxies',
      ],
      'sources': [
        'attributes.cc',
        'chaps.cc',
        'chaps_proxy.cc',
        'chaps_utility.cc',
        'isolate_chromeos.cc',
        'token_manager_client.cc',
      ],
    },
    {
      'target_name': 'chapsd',
      'type': 'executable',
      'dependencies': [
        'chaps-protos',
        'libchaps_static',
      ],
      'variables': {
        'deps': [
          'libmetrics-<(libbase_ver)',
        ],
      },
      'libraries': [
        '-ldl',
        '-lleveldb',
        '-lmemenv',
      ],
      'sources': [
        'chaps_adaptor.cc',
        'chaps_factory_impl.cc',
        'chaps_service.cc',
        'chaps_service_redirect.cc',
        'chapsd.cc',
        'object_impl.cc',
        'object_policy_cert.cc',
        'object_policy_common.cc',
        'object_policy_data.cc',
        'object_policy_key.cc',
        'object_policy_private_key.cc',
        'object_policy_public_key.cc',
        'object_policy_secret_key.cc',
        'object_pool_impl.cc',
        'object_store_impl.cc',
        'opencryptoki_importer.cc',
        'platform_globals_chromeos.cc',
        'session_impl.cc',
        'slot_manager_impl.cc',
      ],
      'conditions': [
        ['USE_tpm2 == 1', {
          'libraries': [
            '-ltrunks',
          ],
          'sources': [
            'tpm2_utility_impl.cc',
          ],
        }],
        ['USE_tpm2 == 0', {
          'libraries': [
            '-ltspi',
          ],
          'sources': [
            'tpm_utility_impl.cc',
          ],
        }],
      ],
    },
    {
      'target_name': 'chaps_client',
      'type': 'executable',
      'dependencies': ['libchaps_static'],
      'sources': [
        'chaps_client.cc',
      ],
    },
    {
      'target_name': 'p11_replay',
      'type': 'executable',
      'dependencies': ['libchaps'],
      'sources': [
        'p11_replay.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'libchaps_test',
          'type': 'static_library',
          'dependencies': [
            'chaps-protos',
            'libchaps_static',
          ],
          'variables': {
            'exported_deps': [
              'libchrome-test-<(libbase_ver)',
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
            'chaps_factory_mock.cc',
            'object_importer_mock.cc',
            'object_mock.cc',
            'object_policy_mock.cc',
            'object_pool_mock.cc',
            'object_store_mock.cc',
            'session_mock.cc',
            'slot_manager_mock.cc',
            'tpm_utility_mock.cc',
          ],
        },
        {
          'target_name': 'chaps_test',
          'type': 'executable',
          'dependencies': ['libchaps_static'],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'chaps_test.cc',
          ],
        },
        {
          'target_name': 'chaps_service_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'chaps_service.cc',
            'chaps_service_test.cc',
          ],
        },
        {
          'target_name': 'slot_manager_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'slot_manager_impl.cc',
            'slot_manager_test.cc',
          ],
        },
        {
          'target_name': 'session_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'session_impl.cc',
            'session_test.cc',
          ],
        },
        {
          'target_name': 'object_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'object_impl.cc',
            'object_test.cc',
          ],
        },
        {
          'target_name': 'object_policy_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'object_policy_cert.cc',
            'object_policy_common.cc',
            'object_policy_data.cc',
            'object_policy_key.cc',
            'object_policy_private_key.cc',
            'object_policy_public_key.cc',
            'object_policy_secret_key.cc',
            'object_policy_test.cc',
          ],
        },
        {
          'target_name': 'object_pool_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'object_pool_impl.cc',
            'object_pool_test.cc',
          ],
        },
        {
          'target_name': 'object_store_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
          ],
          'variables': {
            'deps': [
              'libmetrics-<(libbase_ver)',
            ],
          },
          'libraries': [
            '-lleveldb',
            '-lmemenv',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'object_store_impl.cc',
            'object_store_test.cc',
          ],
        },
        {
          'target_name': 'opencryptoki_importer_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'variables': {
            'deps': [
              'libmetrics-<(libbase_ver)',
            ],
          },
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'opencryptoki_importer.cc',
            'opencryptoki_importer_test.cc',
          ],
        },
        {
          'target_name': 'isolate_login_client_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'variables': {
            'deps': [
              'libmetrics-<(libbase_ver)',
            ],
          },
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'isolate_login_client.cc',
            'isolate_login_client_test.cc',
            'token_file_manager_chromeos.cc',
          ],
        },
        # Live Tests
        # Note: These tests require a live system with gtest and gmock
        # installed. These cannot be run without a real TPM and cannot be
        # run with autotest. These tests do not need to be run regularly
        # but may be useful in the future and so have been kept around.
        {
          'target_name': 'chapsd_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
          ],
          'libraries': [
            '-ldl',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'chaps_service_redirect.cc',
            'chapsd_test.cc',
            'platform_globals_chromeos.cc',
          ],
        },
        {
          'target_name': 'tpm_utility_test',
          'type': 'executable',
          'dependencies': [
            'libchaps_static',
            'libchaps_test',
          ],
          'includes' : ['../common-mk/common_test.gypi'],
          'sources': ['tpm_utility_test.cc',],
          'conditions': [
            ['USE_tpm2 == 1', {
              'libraries': [
                '-ltrunks',
              ],
              'sources': [
                'tpm2_utility_impl.cc',
              ],
            }],
            ['USE_tpm2 == 0', {
              'libraries': [
                '-ltspi',
              ],
              'sources': [
                'tpm_utility_impl.cc',
              ],
            }],
          ],
        },
      ],
      # Conditional Unit Tests
      # Unit tests here are gated by conditionals. Since we cannot add
      # conditionals in the middle of a target list, we added it at the end.
      'conditions': [
        ['USE_tpm2 == 1', {
          'targets': [
            {
              'target_name': 'tpm2_utility_test',
              'type': 'executable',
              'dependencies': [
                'libchaps_static',
                'libchaps_test',
              ],
              'libraries': [
                '-ltrunks',
                '-ltrunks_test',
              ],
              'includes': ['../common-mk/common_test.gypi'],
              'sources': [
                'tpm2_utility_impl.cc',
                'tpm2_utility_test.cc',
              ],
            },
          ],
        }],
      ],
    }],
  ],
}
