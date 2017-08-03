{
  'target_defaults': {
    'variables': {
      'deps': [
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        # system_api depends on protobuf (or protobuf-lite). It must appear
        # before protobuf here or the linker flags won't be in the right
        # order.
        'system_api',
        'protobuf-lite',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libbiod',
      'type': 'static_library',
      'sources': [
        'bio_library.cc',
        'biod_storage.cc',
        'biometrics_daemon.cc',
        'fake_biometrics_manager.cc',
        'fpc_biometrics_manager.cc',
        'scoped_umask.cc',
      ],
    },
    {
      'target_name': 'biod',
      'type': 'executable',
      'variables': {
        'USE_arm%': 0,
        'USE_arm64%': 0,
      },
      'link_settings': {
        'libraries': [
          '-ldl',
          '-L<(sysroot)/opt/fpc/lib/ -lfpalgorithm',
        ],
        'ldflags': [
          # pass --export-dynamic to the linker so libfpsensor.so can see the
          # fp_pal_* symbols at dlopen() time.
          '-Wl,--export-dynamic',
        ],
        'conditions': [
          # For ARM devices, libfpalgorithm and libfpsensor are not completely
          # separated. Thus we need to link against libfpsensor and set -rpath
          # so libfpsensor.so is found by the dynamic linker at runtime.
          ['USE_arm == 1 or USE_arm64 == 1', {
            'libraries': [
              '-lfpsensor',
            ],
            'ldflags': [
              '-Wl,-rpath,/opt/fpc/lib',
            ],
          }],
        ],
      },
      'dependencies': ['libbiod'],
      'sources': [
        'fpc/fpc_platform_utils.cc',
        'main.cc',
      ],
    },
    {
      'target_name': 'biod_client_tool',
      'type': 'executable',
      'sources': ['tools/biod_client_tool.cc'],
    },
    {
      'target_name': 'fake_biometric_tool',
      'type': 'executable',
      'sources': ['tools/fake_biometric_tool.cc'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'biod_test_runner',
          'type': 'executable',
          'dependencies': [
            'libbiod',
            '../common-mk/testrunner.gyp:testrunner',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'variables': {
            'deps': [
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'biod_storage_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
