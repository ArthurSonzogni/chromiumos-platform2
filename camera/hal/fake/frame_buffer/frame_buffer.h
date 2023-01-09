/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_
#define CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_

#include <stdint.h>

#include <memory>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common_types.h"

namespace cros {

// FrameBuffer represents backing buffer of a frame, which might be allocated
// from different sources.
// Basic properties of the buffer includes |size_| which is the resolution of
// the frame, and |fourcc_| represents how the frame pixel is stored in the
// buffer.
class FrameBuffer {
 public:
  class ScopedMapping {
   public:
    ScopedMapping(const ScopedMapping&) = delete;
    ScopedMapping& operator=(const ScopedMapping&) = delete;

    virtual ~ScopedMapping() = 0;

    virtual uint32_t num_planes() const = 0;

    using Plane = cros::ScopedMapping::Plane;
    virtual Plane plane(int plane) const = 0;

   protected:
    ScopedMapping();
  };

  // Returns the mapped buffer, or nullptr if map failed. The return value
  // should not outlive |this|.
  virtual std::unique_ptr<ScopedMapping> Map() = 0;

  Size GetSize() const { return size_; }
  uint32_t GetFourcc() const { return fourcc_; }

  virtual ~FrameBuffer() = 0;

 protected:
  FrameBuffer();

  // Resolution of the frame.
  // If |fourcc_| is V4L2_PIX_FMT_JPEG, then this will be (jpeg_size x 1).
  Size size_;

  // This is V4L2_PIX_FMT_* in linux/videodev2.h.
  uint32_t fourcc_;
};

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_
