# This file is included for every single target.

# Use it to define Platform2 wide settings and defaults.

# gyplint: disable=GypLintVisibilityFlags

{
  'variables': {
    # Set this to 0 if you'd like to disable -Werror.
    'enable_werror%': 1,

    # Set this to 1 if you'd like to enable debugging flags.
    'debug%': 0,

    # Set this to 0 if you'd like to disable -clang-syntax.
    'clang_syntax%': 1,

    # Set this to 1 if your C++ code throws or catches exceptions.
    'enable_exceptions%': 0,

    # Set this to 0 if you want to disable Large File Support (LFS).
    'enable_lfs%': 1,

    # DON'T EDIT BELOW THIS LINE.
    # You can declare new variables, but you shouldn't be changing their
    # default settings.  The platform python build helpers set them up.
    'USE_cheets%': 0,
    'USE_amd64%': 0,
    'USE_attestation%': 0,
    'USE_binder%': 0,
    'USE_buffet%': 0,
    'USE_cellular%': 0,
    'USE_cfm_enabled_device%': 0,
    'USE_chromeos_dbus%': 0,
    'USE_containers%': 0,
    'USE_cros_host%': 0,
    'USE_dbus%': 0,
    'USE_device_mapper%': 0,
    'USE_dhcpv6%': 0,
    'USE_direncryption%': 0,
    'USE_feedback%': 0,
    'USE_ftdi_tpm%': 0,
    'USE_fuzzer%': 0,
    'USE_gdmwimax%': 0,
    'USE_hammerd_api%': 0,
    'USE_iwlwifi_dump%': 0,
    'USE_kvm_host%': 0,
    'USE_metrics_uploader%': 0,
    'USE_mtd%': 0,
    'USE_opengles%': 0,
    'USE_passive_metrics%': 0,
    'USE_power_management%': 0,
    'USE_pppoe%': 0,
    'USE_profiling%': 0,
    'USE_systemd%': 0,
    'USE_tcmalloc%': 0,
    'USE_test%': 0,
    'USE_tpm2%': 0,
    'USE_tpm2_simulator%': 0,
    'USE_vpn%': 0,
    'USE_wake_on_wifi%': 0,
    'USE_wifi%': 0,
    'USE_wimax%': 0,
    'USE_wired_8021x%': 0,

    'external_cflags%': '',
    'external_cxxflags%': '',
    'external_cppflags%': '',
    'external_ldflags%': '',

    'libdir%': '/usr/lib',

    'cflags_no_exceptions': [
      '-fno-exceptions',
      '-fno-unwind-tables',
      '-fno-asynchronous-unwind-tables',
    ],
  },
  'conditions': [
    ['USE_cros_host == 1', {
      'variables': {
        'pkg-config': 'pkg-config',
        'sysroot': '',
      },
    }],
  ],
  'target_defaults': {
    'include_dirs': [
      '<(INTERMEDIATE_DIR)/include',
      '<(SHARED_INTERMEDIATE_DIR)/include',
      '<(platform2_root)',
      '<(platform2_root)/../platform', # src/platform
    ],
    'cflags': [
      '-Wall',
      '-Wno-psabi',
      '-Wunused',
      '-Wno-unused-parameter',
      '-ggdb3',
      '-fstack-protector-strong',
      '-Wformat=2',
      '-fvisibility=internal',
      '-Wa,--noexecstack',
    ],
    'cflags_c': [
      '-std=gnu11',
      '<@(external_cppflags)',
      '<@(external_cflags)',
    ],
    'cflags_cc': [
      '-std=gnu++14',
      '<@(external_cppflags)',
      '<@(external_cxxflags)',
    ],
    'link_settings': {
      'ldflags': [
        '<(external_ldflags)',
        '-Wl,-z,relro',
        '-Wl,-z,noexecstack',
        '-Wl,-z,now',
        '-Wl,--as-needed',
      ],
      'target_conditions': [
        ['deps != []', {
          # We don't care about getting rid of duplicate ldflags, so we use
          # @ to expand to list.
          'ldflags+': [
            '>!@(<(pkg-config) >(deps) --libs-only-L --libs-only-other)',
          ],
          'libraries+': [
            # Note there's no @ here intentionally, we want to keep libraries
            # returned by pkg-config as a single string in order to maintain
            # order for linking (rather than a list of individual libraries
            # which GYP would make unique for us).
            '>!(<(pkg-config) >(deps) --libs-only-l)',
          ],
        }],
      ],
    },
    'variables': {
      'deps%': [],
    },
    'conditions': [
      ['enable_werror == 1', {
        'cflags+': [
          '-Werror',
        ],
      }],
      ['USE_cros_host == 1', {
        'defines': [
          'NDEBUG',
        ],
      }],
      ['enable_lfs == 1', {
        'defines': [
          # Enable support for new LFS funcs (ftello/etc...).
          '_LARGEFILE_SOURCE',
          # Enable support for 64bit variants (off64_t/fseeko64/etc...).
          '_LARGEFILE64_SOURCE',
          # Default to 64bit variants (off_t is defined as off64_t).
          '_FILE_OFFSET_BITS=64',
        ],
      }],
      ['USE_cros_host == 0 and clang_syntax == 1', {
        'cflags': [
          '-clang-syntax',
        ],
      }],
      ['enable_exceptions == 0', {
        'cflags_cc': [
          '<@(cflags_no_exceptions)',
        ],
        'cflags_c': [
          '<@(cflags_no_exceptions)',
        ],
      }],
      ['enable_exceptions == 1', {
        # Remove the no-exceptions flags from both cflags_cc and cflags_c.
        'cflags_cc!': [
          '<@(cflags_no_exceptions)',
        ],
        'cflags_c!': [
          '<@(cflags_no_exceptions)',
        ],
      }],
      ['USE_cros_host == 0', {
        'include_dirs': [
          '<(sysroot)/usr/include',
        ],
        'cflags': [
          '--sysroot=<(sysroot)',
        ],
        'link_settings': {
          'ldflags': [
            '--sysroot=<(sysroot)',
          ],
        },
      }],
      ['USE_profiling == 1', {
        'cflags': ['-fprofile-instr-generate', '-fcoverage-mapping'],
        'ldflags': ['-fprofile-instr-generate', '-fcoverage-mapping'],
      }],
      ['USE_tcmalloc == 1', {
        'link_settings': {
          'libraries': [
            '-ltcmalloc',
          ],
        },
      }],
    ],
    'target_conditions': [
      ['deps != []', {
        'cflags+': [
          # We don't care about getting rid of duplicate cflags, so we use
          # @ to expand to list.
          '>!@(<(pkg-config) >(deps) --cflags)',
        ],
      }],
      ['_type == "executable"', {
        'cflags': ['-fPIE'],
        'ldflags': ['-pie'],
      }],
      ['_type == "static_library"', {
        'cflags': ['-fPIE'],
      }],
      ['_type == "shared_library"', {
        'cflags': ['-fPIC'],
      }],
      ['OS!="win"', {
        'sources/': [
          ['exclude', '_win(_browsertest|_unittest)?\\.(h|cc)$'],
          ['exclude', '(^|/)win/'],
          ['exclude', '(^|/)win_[^/]*\\.(h|cc)$'],
        ],
      }],
      ['OS!="mac"', {
        'sources/': [
          ['exclude', '_(cocoa|mac)(_unittest)?\\.(h|cc|mm?)$'],
          ['exclude', '(^|/)(cocoa|mac)/'],
        ],
      }],
      ['OS!="ios"', {
        'sources/': [
          ['exclude', '_ios(_unittest)?\\.(h|cc|mm?)$'],
          ['exclude', '(^|/)ios/'],
        ],
      }],
      ['(OS!="mac" and OS!="ios")', {
        'sources/': [ ['exclude', '\\.mm?$' ] ],
      }],
    ],
  },
}
