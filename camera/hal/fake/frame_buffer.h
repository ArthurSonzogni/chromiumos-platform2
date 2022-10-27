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
#include "cros-camera/common_types.h"

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

  Size GetSize() const { return size_; }
  uint32_t GetFourcc() const { return fourcc_; }

  ~FrameBuffer();

  buffer_handle_t GetBufferHandle() const { return buffer_; }

  // Wraps external buffer from upper framework. Fill |size_| according to the
  // parameters. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Wrap(buffer_handle_t buffer, Size size);

  // Allocates the buffer internally. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Create(Size size,
                                             android_pixel_format_t fourcc);

 private:
  FrameBuffer();

  // Wraps external buffer from upper framework. Fill |size_| according to the
  // parameters.
  bool Initialize(buffer_handle_t buffer, Size size);

  // Allocate the buffer internally.
  bool Initialize(Size size, android_pixel_format_t fourcc);

  // Frame resolution.
  Size size_;

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
