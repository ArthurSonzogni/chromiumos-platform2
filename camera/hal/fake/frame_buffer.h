/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_FRAME_BUFFER_H_
#define CAMERA_HAL_FAKE_FRAME_BUFFER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <absl/status/statusor.h>
#include <base/sequence_checker.h>

#include "cros-camera/camera_buffer_manager.h"

namespace cros {

// FrameBuffer uses CameraBufferManager to manage the buffer.
// The class is not thread safe and all methods should be run on the same
// sequence.
class FrameBuffer {
 public:
  enum {
    kYPlane = 0,
    kUPlane = 1,
    kVPlane = 2,
  };

  // Returns the mapped buffer. The return value should not outlive |this|.
  absl::StatusOr<ScopedMapping> Map();

  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  uint32_t GetFourcc() const { return fourcc_; }

  ~FrameBuffer();

  buffer_handle_t GetBufferHandle() const { return buffer_; }

  // Wraps external buffer from upper framework. Fill |width_| and |height_|
  // according to the parameters. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Wrap(buffer_handle_t buffer,
                                           uint32_t width,
                                           uint32_t height);

  // Allocates the buffer internally. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Create(uint32_t width,
                                             uint32_t height,
                                             android_pixel_format_t fourcc);

 private:
  FrameBuffer();

  // Wraps external buffer from upper framework. Fill |width_| and |height_|
  // according to the parameters.
  bool Initialize(buffer_handle_t buffer, uint32_t width, uint32_t height);

  // Allocate the buffer internally.
  bool Initialize(uint32_t width,
                  uint32_t height,
                  android_pixel_format_t fourcc);

  // Frame resolution.
  uint32_t width_;
  uint32_t height_;

  // This is V4L2_PIX_FMT_* in linux/videodev2.h.
  uint32_t fourcc_;

  // The currently used buffer.
  buffer_handle_t buffer_ = nullptr;

  // Used to import gralloc buffer.
  CameraBufferManager* buffer_manager_;

  // Whether the |buffer_| is allocated by this class.
  bool is_buffer_owned_ = false;

  // Use to check all methods are called on the same thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FRAME_BUFFER_H_
