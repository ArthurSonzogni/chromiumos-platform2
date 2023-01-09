/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/frame_buffer/cpu_memory_frame_buffer.h"

#include <sys/mman.h>

#include <utility>

#include <base/memory/ptr_util.h>
#include <base/numerics/checked_math.h>
#include <hardware/gralloc.h>
#include <linux/videodev2.h>
#include <libyuv.h>

#include "cros-camera/common.h"

namespace cros {

CpuMemoryFrameBuffer::ScopedMapping::ScopedMapping(
    base::SafeRef<CpuMemoryFrameBuffer> buffer)
    : buffer_(buffer) {}

CpuMemoryFrameBuffer::ScopedMapping::~ScopedMapping() = default;

uint32_t CpuMemoryFrameBuffer::ScopedMapping::num_planes() const {
  return buffer_->planes_.size();
}

FrameBuffer::ScopedMapping::Plane CpuMemoryFrameBuffer::ScopedMapping::plane(
    int plane) const {
  return buffer_->planes_[plane].plane;
}

// static
std::unique_ptr<CpuMemoryFrameBuffer> CpuMemoryFrameBuffer::Create(
    Size size, uint32_t fourcc) {
  auto frame_buffer = base::WrapUnique(new CpuMemoryFrameBuffer());
  if (!frame_buffer->Initialize(size, fourcc)) {
    return nullptr;
  }
  return frame_buffer;
}

CpuMemoryFrameBuffer::CpuMemoryFrameBuffer() = default;

bool CpuMemoryFrameBuffer::Initialize(Size size, uint32_t fourcc) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_ = size;
  fourcc_ = fourcc;

  switch (fourcc) {
    case V4L2_PIX_FMT_NV12:
      // TODO(pihsun): Support odd width / height by doing rounding up.
      if (size.width % 2 != 0 || size.height % 2 != 0) {
        LOGF(WARNING) << "Buffer width and height should both be even";
        return false;
      }
      planes_.emplace_back(AllocatePlane(size));
      planes_.emplace_back(AllocatePlane(Size(size.width, size.height / 2)));
      break;

    case V4L2_PIX_FMT_YUV420:
      // TODO(pihsun): Support odd width / height by doing rounding up.
      if (size.width % 2 != 0 || size.height % 2 != 0) {
        LOGF(WARNING) << "Buffer width and height should both be even";
        return false;
      }
      planes_.emplace_back(AllocatePlane(size));
      planes_.emplace_back(
          AllocatePlane(Size(size.width / 2, size.height / 2)));
      planes_.emplace_back(
          AllocatePlane(Size(size.width / 2, size.height / 2)));
      break;

    case V4L2_PIX_FMT_JPEG:
      planes_.emplace_back(AllocatePlane(size));
      break;

    default:
      LOGF(WARNING) << "Unsupported format " << FormatToString(fourcc);
      return false;
  }

  return true;
}

CpuMemoryFrameBuffer::~CpuMemoryFrameBuffer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::unique_ptr<FrameBuffer::ScopedMapping> CpuMemoryFrameBuffer::Map() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return base::WrapUnique(new ScopedMapping(weak_ptr_factory_.GetSafeRef()));
}

CpuMemoryFrameBuffer::StoredPlane CpuMemoryFrameBuffer::AllocatePlane(
    Size size) {
  size_t memory_size =
      base::CheckMul(size.width, size.height).ValueOrDie<size_t>();
  std::unique_ptr<uint8_t[]> data(new uint8_t[memory_size]);

  return {
      .plane =
          ScopedMapping::Plane{
              .addr = data.get(),
              .stride = size.width,
              .size = memory_size,
          },
      .data = std::move(data),
  };
}

}  // namespace cros
