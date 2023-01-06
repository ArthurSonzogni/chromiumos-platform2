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
#include "hal/fake/frame_buffer/frame_buffer.h"
#include "hal/fake/frame_buffer/gralloc_frame_buffer.h"
#include "hal/fake/hal_spec.h"

namespace cros {
class FakeStream {
 public:
  FakeStream(FakeStream&&) = delete;
  FakeStream& operator=(FakeStream&&) = delete;

  FakeStream(const FakeStream&) = delete;
  FakeStream& operator=(const FakeStream&) = delete;

  virtual ~FakeStream();

  // Factory method to create a FakeStream, might return null on error.
  static std::unique_ptr<FakeStream> Create(
      const android::CameraMetadata& static_metadata,
      Size size,
      android_pixel_format_t format,
      const FramesSpec& spec);

  // Fills the buffer with the next frame from the fake stream. The buffer
  // format should match the format specified in the constructor.
  [[nodiscard]] virtual bool FillBuffer(buffer_handle_t buffer) = 0;

 protected:
  FakeStream();

  CameraBufferManager* buffer_manager_;

  uint32_t jpeg_max_size_ = 0;

  // JPEG compressor instance
  std::unique_ptr<JpegCompressor> jpeg_compressor_;

  Size size_;

  android_pixel_format_t format_;

  // Map and copy the content of the buffer to output buffer.
  [[nodiscard]] bool CopyBuffer(FrameBuffer& buffer,
                                buffer_handle_t output_buffer);

  [[nodiscard]] virtual bool Initialize(
      const android::CameraMetadata& static_metadata,
      Size size,
      android_pixel_format_t format,
      const FramesSpec& spec);
};

class StaticFakeStream : public FakeStream {
 protected:
  friend class FakeStream;
  explicit StaticFakeStream(std::unique_ptr<GrallocFrameBuffer> buffer);

  [[nodiscard]] bool Initialize(const android::CameraMetadata& static_metadata,
                                Size size,
                                android_pixel_format_t format,
                                const FramesSpec& spec) override;

  [[nodiscard]] bool FillBuffer(buffer_handle_t buffer) override;

 private:
  std::unique_ptr<GrallocFrameBuffer> buffer_;
};
}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FAKE_STREAM_H_
