{
  'includes': [
    '../build/cros-camera-common.gypi',
  ],
  'target_defaults': {
    'variables': {
      'deps': [
        'libyuv',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libcamera_jpeg',
      'includes': [
        '../build/standalone_static_library.gypi',
      ],
      'dependencies': [
        'jpeg/libjea.gyp:libjea',
      ],
      'direct_dependent_settings': {
        'libraries': [
          '-ljpeg',
        ],
      },
      'sources': [
        'jpeg_compressor_impl.cc',
      ],
    },
  ],
}
