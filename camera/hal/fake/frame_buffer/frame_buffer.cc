/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/frame_buffer/frame_buffer.h"

#include <libyuv.h>

namespace cros {

FrameBuffer::ScopedMapping::ScopedMapping() = default;
FrameBuffer::ScopedMapping::~ScopedMapping() = default;

FrameBuffer::FrameBuffer() = default;
FrameBuffer::~FrameBuffer() = default;

// static
bool FrameBuffer::ScaleInto(FrameBuffer& buffer, FrameBuffer& output_buffer) {
  if (buffer.GetFourcc() != V4L2_PIX_FMT_NV12 ||
      output_buffer.GetFourcc() != V4L2_PIX_FMT_NV12) {
    LOGF(WARNING) << "Only V4L2_PIX_FMT_NV12 is supported for resize";
    return false;
  }

  auto mapped_buffer = buffer.Map();
  if (mapped_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map temporary buffer";
    return false;
  }

  auto y_plane = mapped_buffer->plane(0);
  auto uv_plane = mapped_buffer->plane(1);

  auto mapped_output_buffer = output_buffer.Map();
  if (mapped_output_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map buffer";
    return false;
  }

  auto output_y_plane = mapped_output_buffer->plane(0);
  auto output_uv_plane = mapped_output_buffer->plane(1);

  auto size = output_buffer.GetSize();

  // TODO(pihsun): Support "object-fit" for different scaling method.
  int ret = libyuv::NV12Scale(
      y_plane.addr, y_plane.stride, uv_plane.addr, uv_plane.stride,
      buffer.GetSize().width, buffer.GetSize().height, output_y_plane.addr,
      output_y_plane.stride, output_uv_plane.addr, output_uv_plane.stride,
      size.width, size.height, libyuv::kFilterBilinear);
  if (ret != 0) {
    LOGF(WARNING) << "NV12Scale() failed with " << ret;
    return false;
  }

  return true;
}

}  // namespace cros
