/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/fake_stream.h"

#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/numerics/clamped_math.h>
#include <base/timer/elapsed_timer.h>
#include <libyuv.h>
#include <linux/videodev2.h>

#include "hal/fake/camera_hal.h"
#include "hal/fake/test_pattern.h"

namespace cros {

namespace {
std::unique_ptr<FrameBuffer> ReadMJPGFromFile(const base::FilePath& path,
                                              Size size) {
  auto bytes = base::ReadFileToBytes(path);
  if (!bytes.has_value()) {
    LOGF(WARNING) << "Failed to read file: " << path;
    return nullptr;
  }
  int width, height;
  if (libyuv::MJPGSize(bytes->data(), bytes->size(), &width, &height) != 0) {
    LOGF(WARNING) << "Failed to get MJPG size: " << path;
    return nullptr;
  }
  CHECK(width > 0 && height > 0);
  if (width > 8192 || height > 8192) {
    LOGF(WARNING) << "Image size too large: " << path;
    return nullptr;
  }

  auto temp_buffer =
      FrameBuffer::Create(Size(width, height), HAL_PIXEL_FORMAT_YCbCr_420_888);
  if (temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to create temporary buffer";
    return nullptr;
  }

  auto mapped_temp_buffer = temp_buffer->Map();
  if (!mapped_temp_buffer.ok()) {
    LOGF(WARNING) << "Failed to map temporary buffer";
    return nullptr;
  }

  auto temp_y_plane = mapped_temp_buffer->plane(0);
  auto temp_uv_plane = mapped_temp_buffer->plane(1);
  DCHECK(temp_y_plane.addr != nullptr);
  DCHECK(temp_uv_plane.addr != nullptr);

  int ret = libyuv::MJPGToNV12(
      bytes->data(), bytes->size(), temp_y_plane.addr, temp_y_plane.stride,
      temp_uv_plane.addr, temp_uv_plane.stride, width, height, width, height);
  if (ret != 0) {
    LOGF(WARNING) << "MJPGToNV12() failed with " << ret;
    return nullptr;
  }

  auto buffer = FrameBuffer::Create(size, HAL_PIXEL_FORMAT_YCbCr_420_888);
  if (buffer == nullptr) {
    LOGF(WARNING) << "Failed to create buffer";
    return nullptr;
  }

  auto mapped_buffer = buffer->Map();
  if (!mapped_buffer.ok()) {
    LOGF(WARNING) << "Failed to map buffer";
    return nullptr;
  }

  auto y_plane = mapped_buffer->plane(0);
  auto uv_plane = mapped_buffer->plane(1);
  DCHECK(y_plane.addr != nullptr);
  DCHECK(uv_plane.addr != nullptr);

  // TODO(pihsun): Support "object-fit" for different scaling method.
  ret = libyuv::NV12Scale(temp_y_plane.addr, temp_y_plane.stride,
                          temp_uv_plane.addr, temp_uv_plane.stride, width,
                          height, y_plane.addr, y_plane.stride, uv_plane.addr,
                          uv_plane.stride, size.width, size.height,
                          libyuv::kFilterBilinear);
  if (ret != 0) {
    LOGF(WARNING) << "NV12Scale() failed with " << ret;
    return nullptr;
  }

  return buffer;
}
}  // namespace

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
    android_pixel_format_t format,
    const FramesSpec& spec) {
  auto fake_stream = base::WrapUnique(new FakeStream());
  if (!fake_stream->Initialize(static_metadata, size, format, spec)) {
    return nullptr;
  }
  return fake_stream;
}

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

bool FakeStream::Initialize(const android::CameraMetadata& static_metadata,
                            Size size,
                            android_pixel_format_t format,
                            const FramesSpec& spec) {
  camera_metadata_ro_entry_t entry =
      static_metadata.find(ANDROID_JPEG_MAX_SIZE);
  if (entry.count == 0) {
    LOGF(WARNING) << "JPEG max size not found in static metadata";
    return false;
  }
  jpeg_max_size_ = entry.data.i32[0];

  size_ = size;
  format_ = format;

  std::unique_ptr<FrameBuffer> input_buffer = std::visit(
      Overloaded{
          [&size](const FramesTestPatternSpec& spec) {
            return GenerateTestPattern(
                size, ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY);
          },
          [&size](const FramesFileSpec& spec) {
            // TODO(pihsun): Handle different file type based on file
            // extension.
            // TODO(pihsun): This only reads a single frame now, read and
            // convert the whole stream on fly.
            return ReadMJPGFromFile(spec.path, size);
          },
      },
      spec);
  if (!input_buffer) {
    LOGF(WARNING) << "Failed to generate input buffer";
    return false;
  }

  if (format == HAL_PIXEL_FORMAT_BLOB) {
    buffer_ = FrameBuffer::Create(Size(jpeg_max_size_, 1), format);
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
  auto frame_buffer = FrameBuffer::Wrap(buffer, size);
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
