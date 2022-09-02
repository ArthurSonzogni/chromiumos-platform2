/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/fake_stream.h"

#include <vector>

#include <absl/cleanup/cleanup.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/numerics/clamped_math.h>
#include <base/timer/elapsed_timer.h>
#include <linux/videodev2.h>

#include "cros-camera/common.h"
#include "hal/fake/camera_hal.h"

namespace cros {

FakeStream::FakeStream()
    : buffer_manager_(CameraBufferManager::GetInstance()),
      jpeg_compressor_(JpegCompressor::GetInstance(
          CameraHal::GetInstance().GetMojoManagerToken())) {}

FakeStream::FakeStream(FakeStream&&) = default;

FakeStream& FakeStream::operator=(FakeStream&&) = default;

FakeStream::~FakeStream() {
  if (buffer_ != nullptr) {
    if (buffer_manager_->Free(buffer_) != 0) {
      LOGF(WARNING) << "failed to free the buffer";
    }
  }
}

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
  DCHECK(!initialized_);

  camera_metadata_ro_entry_t entry =
      static_metadata.find(ANDROID_JPEG_MAX_SIZE);
  if (entry.count == 0) {
    LOGF(WARNING) << "JPEG max size not found in static metadata";
    return false;
  }
  jpeg_max_size_ = entry.data.i32[0];

  size_ = size;
  format_ = format;

  uint32_t hal_usage =
      GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
  uint32_t stride;

  if (format == HAL_PIXEL_FORMAT_BLOB) {
    int ret = buffer_manager_->Allocate(jpeg_max_size_, 1, format, hal_usage,
                                        &buffer_, &stride);
    if (ret != 0) {
      LOGF(WARNING) << "Failed to allocate JPEG buffer";
      return false;
    }

    uint32_t out_data_size;

    {
      auto input_buffer = buffer_manager_->AllocateScopedBuffer(
          size.width, size.height, HAL_PIXEL_FORMAT_YCbCr_420_888, hal_usage);
      if (!input_buffer) {
        LOGF(WARNING) << "Failed to allocate temporary buffer";
        return false;
      }

      std::vector<uint8_t> app1;
      // TODO(pihsun): Fill thumbnail in app1.
      // TODO(pihsun): Should use android.jpeg.quality in request metadata for
      // JPEG quality. Cache the frame using default quality in request
      // template, and redo JPEG encoding when quality changes.
      bool success = jpeg_compressor_->CompressImageFromHandle(
          *input_buffer, buffer_, size.width, size.height, /*quality=*/90,
          app1.data(), app1.size(), &out_data_size);

      if (!success) {
        LOGF(WARNING) << "JPEG encode failed";
        return false;
      }
    }

    void* addr;
    ret = buffer_manager_->Lock(buffer_, 0, 0, 0, 0, 0, &addr);
    if (ret != 0) {
      LOGF(WARNING) << "buffer mapping failed";
      return false;
    }

    auto data = static_cast<uint8_t*>(addr);

    camera3_jpeg_blob_t blob = {
        .jpeg_blob_id = CAMERA3_JPEG_BLOB_ID,
        .jpeg_size = out_data_size,
    };

    CHECK(base::ClampAdd(out_data_size, sizeof(blob)) <= jpeg_max_size_);
    memcpy(data + jpeg_max_size_ - sizeof(blob), &blob, sizeof(blob));

    if (buffer_manager_->Unlock(buffer_) != 0) {
      LOGF(WARNING) << "failed to unlock the buffer";
      return false;
    }
  } else if (format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
    // Do nothing for now.
    // TODO(pihsun): Fill buffer data for test pattern / image from spec.
  } else {
    NOTIMPLEMENTED() << "format = " << format << " is not supported";
    return false;
  }

  initialized_ = true;
  return true;
}

bool FakeStream::FillBuffer(buffer_handle_t buffer) {
  DCHECK(initialized_);

  int ret = buffer_manager_->Register(buffer);
  if (ret != 0) {
    LOGF(WARNING) << "failed to register the buffer";
    return false;
  }
  absl::Cleanup buffer_deregister = [&] {
    // TODO(pihsun): Should we abort the request if cleanup fails?
    if (buffer_manager_->Deregister(buffer) != 0) {
      LOGF(WARNING) << "failed to deregister the buffer";
    }
  };

  if (format_ == HAL_PIXEL_FORMAT_BLOB) {
    DCHECK_EQ(CameraBufferManager::GetV4L2PixelFormat(buffer),
              V4L2_PIX_FMT_JPEG);

    void *src, *dst;
    int from_buffer_size = buffer_manager_->GetPlaneSize(buffer_, 0);
    ret = buffer_manager_->Lock(buffer_, 0, 0, 0, 0, 0, &src);
    if (ret != 0) {
      LOGF(WARNING) << "failed to map the buffer";
      return false;
    }
    absl::Cleanup from_buffer_unlock = [&] {
      if (buffer_manager_->Unlock(buffer_) != 0) {
        LOGF(WARNING) << "failed to unlock the buffer";
      }
    };

    int to_buffer_size = buffer_manager_->GetPlaneSize(buffer, 0);
    // Since the camera3_jpeg_blob_t "header" is located at the end of the
    // buffer, we requires the output to be the same size as the cached buffer.
    // They should both be the size of jpeg_max_size_.
    // TODO(pihsun): Only copy the JPEG part and append the camera3_jpeg_blob_t
    // per frame?
    DCHECK_EQ(from_buffer_size, to_buffer_size);
    ret = buffer_manager_->Lock(buffer, 0, 0, 0, 0, 0, &dst);
    if (ret != 0) {
      LOGF(WARNING) << "failed to map the buffer";
      return false;
    }
    absl::Cleanup to_buffer_unlock = [&] {
      if (buffer_manager_->Unlock(buffer) != 0) {
        LOGF(WARNING) << "failed to unlock the buffer";
      }
    };

    memcpy(dst, src, to_buffer_size);
  } else if (format_ == HAL_PIXEL_FORMAT_YCBCR_420_888) {
    // TODO(pihsun): Do conversion for HAL_PIXEL_FORMAT_YCBCR_420_888
    // leave the content in buffer untouched for now.
  } else {
    NOTREACHED() << "unknown format " << format_;
  }

  return true;
}

}  // namespace cros
