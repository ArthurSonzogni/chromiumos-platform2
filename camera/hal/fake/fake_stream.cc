/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/fake_stream.h"

#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/numerics/clamped_math.h>
#include <base/timer/elapsed_timer.h>
#include <linux/videodev2.h>

#include "hal/fake/camera_hal.h"

namespace cros {

FakeStream::FakeStream()
    : buffer_manager_(CameraBufferManager::GetInstance()),
      jpeg_compressor_(JpegCompressor::GetInstance(
          CameraHal::GetInstance().GetMojoManagerToken())) {}

FakeStream::FakeStream(FakeStream&&) = default;

FakeStream& FakeStream::operator=(FakeStream&&) = default;

FakeStream::~FakeStream() = default;

// static
std::unique_ptr<FakeStream> FakeStream::Create(
    const android::CameraMetadata& static_metadata,
    Size size,
    android_pixel_format_t format) {
  auto fake_stream = base::WrapUnique(new FakeStream());
  if (!fake_stream->Initialize(static_metadata, size, format)) {
    return nullptr;
  }
  return fake_stream;
}

bool FakeStream::Initialize(const android::CameraMetadata& static_metadata,
                            Size size,
                            android_pixel_format_t format) {
  camera_metadata_ro_entry_t entry =
      static_metadata.find(ANDROID_JPEG_MAX_SIZE);
  if (entry.count == 0) {
    LOGF(WARNING) << "JPEG max size not found in static metadata";
    return false;
  }
  jpeg_max_size_ = entry.data.i32[0];

  size_ = size;
  format_ = format;

  auto input_buffer = FrameBuffer::Create(size.width, size.height,
                                          HAL_PIXEL_FORMAT_YCbCr_420_888);
  if (!input_buffer) {
    LOGF(WARNING) << "Failed to allocate a temporary buffer";
    return false;
  }
  // TODO(pihsun): Fill test pattern.

  if (format == HAL_PIXEL_FORMAT_BLOB) {
    buffer_ = FrameBuffer::Create(jpeg_max_size_, 1, format);
    if (!buffer_) {
      return false;
    }

    uint32_t out_data_size;

    std::vector<uint8_t> app1;
    // TODO(pihsun): Fill thumbnail in app1.
    // TODO(pihsun): Should use android.jpeg.quality in request metadata for
    // JPEG quality. Cache the frame using default quality in request
    // template, and redo JPEG encoding when quality changes.
    bool success = jpeg_compressor_->CompressImageFromHandle(
        input_buffer->GetBufferHandle(), buffer_->GetBufferHandle(), size.width,
        size.height, /*quality=*/90, app1.data(), app1.size(), &out_data_size);
    if (!success) {
      LOGF(WARNING) << "failed to encode JPEG";
      return false;
    }

    auto mapped_buffer = buffer_->Map();
    if (!mapped_buffer.ok()) {
      LOGF(WARNING) << "failed to map the buffer: " << mapped_buffer.status();
      return false;
    }

    auto data = mapped_buffer->plane(0).addr;

    camera3_jpeg_blob_t blob = {
        .jpeg_blob_id = CAMERA3_JPEG_BLOB_ID,
        .jpeg_size = out_data_size,
    };

    CHECK(base::ClampAdd(out_data_size, sizeof(blob)) <= jpeg_max_size_);
    memcpy(data + jpeg_max_size_ - sizeof(blob), &blob, sizeof(blob));
  } else if (format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
    // TODO(pihsun): Fill buffer data for test pattern / image from spec.
    buffer_ = std::move(input_buffer);
  } else {
    NOTIMPLEMENTED() << "format = " << format << " is not supported";
    return false;
  }

  return true;
}

bool FakeStream::FillBuffer(buffer_handle_t buffer, Size size) {
  auto frame_buffer = FrameBuffer::Wrap(buffer, size.width, size.height);
  if (!frame_buffer) {
    LOGF(WARNING) << "failed to register the input buffer";
    return false;
  }

  if (format_ == HAL_PIXEL_FORMAT_BLOB) {
    DCHECK_EQ(frame_buffer->GetFourcc(), V4L2_PIX_FMT_JPEG);
  } else if (format_ == HAL_PIXEL_FORMAT_YCBCR_420_888) {
    // TODO(pihsun): For HAL_PIXEL_FORMAT_YCBCR_420_888 there should be libyuv
    // conversion.
    DCHECK_EQ(frame_buffer->GetFourcc(), V4L2_PIX_FMT_NV12);
  } else {
    NOTREACHED() << "unknown format " << format_;
  }

  auto mapped_buffer = buffer_->Map();
  if (!mapped_buffer.ok()) {
    LOGF(WARNING) << "failed to map the fake stream buffer: "
                  << mapped_buffer.status();
    return false;
  }

  auto mapped_frame_buffer = frame_buffer->Map();
  if (!mapped_frame_buffer.ok()) {
    LOGF(WARNING) << "failed to map the input buffer: "
                  << mapped_frame_buffer.status();
    return false;
  }

  CHECK_EQ(mapped_buffer->num_planes(), mapped_frame_buffer->num_planes());
  for (size_t i = 0; i < mapped_buffer->num_planes(); i++) {
    // Since the camera3_jpeg_blob_t "header" is located at the end of the
    // buffer, we requires the output to be the same size as the cached buffer.
    // They should both be the size of jpeg_max_size_.
    // TODO(pihsun): Only copy the JPEG part and append the camera3_jpeg_blob_t
    // per frame?
    auto src_plane = mapped_buffer->plane(i);
    auto dst_plane = mapped_frame_buffer->plane(i);
    CHECK_EQ(src_plane.size, dst_plane.size);
    memcpy(dst_plane.addr, src_plane.addr, dst_plane.size);
  }

  return true;
}

}  // namespace cros
