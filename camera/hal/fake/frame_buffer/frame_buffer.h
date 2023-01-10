/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_
#define CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_

#include <stdint.h>

#include <memory>
#include <type_traits>

#include <base/memory/ptr_util.h>
#include <linux/videodev2.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
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

    // Returns the number of planes in the mapped buffer.
    virtual uint32_t num_planes() const = 0;

    using Plane = cros::ScopedMapping::Plane;

    // Returns the plane of given index. The given index should be in range of
    // [0, |num_planes()|) and the returned plane should have non-null address.
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

  // Allocates the buffer internally. Returns nullptr when there's an error.
  template <typename T,
            typename = std::enable_if_t<std::is_base_of_v<FrameBuffer, T>>>
  static std::unique_ptr<T> Create(Size size, uint32_t fourcc);

  // Scales to the given size and return the new buffer. Only supports
  // V4L2_PIX_FMT_NV12 for now. Returns nullptr when there's an error.
  template <typename T,
            typename = std::enable_if_t<std::is_base_of_v<FrameBuffer, T>>>
  static std::unique_ptr<T> Scale(FrameBuffer& buffer, Size size);

 protected:
  FrameBuffer();

  [[nodiscard]] virtual bool Initialize(Size size, uint32_t fourcc) = 0;

  // Scales to the given size. Both the input and output buffer should be
  // V4L2_PIX_FMT_NV12 for now.
  [[nodiscard]] static bool ScaleInto(FrameBuffer& buffer,
                                      FrameBuffer& output_buffer);

  // Resolution of the frame.
  // If |fourcc_| is V4L2_PIX_FMT_JPEG, then this will be (jpeg_size x 1).
  Size size_;

  // This is V4L2_PIX_FMT_* in linux/videodev2.h.
  uint32_t fourcc_;
};

// static
template <typename T, typename>
std::unique_ptr<T> FrameBuffer::Create(Size size, uint32_t fourcc) {
  auto frame_buffer = base::WrapUnique(new T());
  if (!frame_buffer->Initialize(size, fourcc)) {
    return nullptr;
  }
  return frame_buffer;
}

// static
template <typename T, typename>
std::unique_ptr<T> FrameBuffer::Scale(FrameBuffer& buffer, Size size) {
  auto output_buffer = FrameBuffer::Create<T>(size, V4L2_PIX_FMT_NV12);
  if (output_buffer == nullptr) {
    LOGF(WARNING) << "Failed to create buffer";
    return nullptr;
  }
  if (!FrameBuffer::ScaleInto(buffer, *output_buffer)) {
    LOGF(WARNING) << "Failed to resize buffer";
    return nullptr;
  }
  return output_buffer;
}

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FRAME_BUFFER_FRAME_BUFFER_H_
