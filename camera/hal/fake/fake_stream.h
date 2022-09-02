/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_FAKE_STREAM_H_
#define CAMERA_HAL_FAKE_FAKE_STREAM_H_

#include <memory>

#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common_types.h"
#include "cros-camera/jpeg_compressor.h"

namespace cros {
class FakeStream {
 public:
  FakeStream(FakeStream&&);
  FakeStream& operator=(FakeStream&&);

  FakeStream(const FakeStream&) = delete;
  FakeStream& operator=(const FakeStream&) = delete;

  ~FakeStream();

  // Factory method to create a FakeStream, might return null on error.
  static std::unique_ptr<FakeStream> Create(
      const android::CameraMetadata& static_metadata,
      Size size,
      android_pixel_format_t format);

  // Fills the buffer with the next frame from the fake stream. The buffer
  // format should match the format specified in the constructor.
  bool FillBuffer(buffer_handle_t buffer);

 private:
  FakeStream();

  bool Initialize(const android::CameraMetadata& static_metadata,
                  Size size,
                  android_pixel_format_t format);

  CameraBufferManager* buffer_manager_;

  uint32_t jpeg_max_size_ = 0;

  buffer_handle_t buffer_ = nullptr;

  // JPEG compressor instance
  std::unique_ptr<JpegCompressor> jpeg_compressor_;

  bool initialized_ = false;

  Size size_;

  android_pixel_format_t format_;
};
}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FAKE_STREAM_H_
