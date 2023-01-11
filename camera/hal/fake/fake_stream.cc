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
#include "hal/fake/frame_buffer/gralloc_frame_buffer.h"
#include "hal/fake/test_pattern.h"
#include "hal/fake/y4m_fake_stream.h"

namespace cros {

namespace {
std::unique_ptr<GrallocFrameBuffer> ReadMJPGFromFile(const base::FilePath& path,
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
  if (width > kFrameMaxDimension || height > kFrameMaxDimension) {
    LOGF(WARNING) << "Image size too large: " << path;
    return nullptr;
  }

  auto temp_buffer = GrallocFrameBuffer::Create(Size(width, height),
                                                HAL_PIXEL_FORMAT_YCbCr_420_888);
  if (temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to create temporary buffer";
    return nullptr;
  }

  auto mapped_temp_buffer = temp_buffer->Map();
  if (mapped_temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map temporary buffer";
    return nullptr;
  }

  auto temp_y_plane = mapped_temp_buffer->plane(0);
  auto temp_uv_plane = mapped_temp_buffer->plane(1);

  int ret = libyuv::MJPGToNV12(
      bytes->data(), bytes->size(), temp_y_plane.addr, temp_y_plane.stride,
      temp_uv_plane.addr, temp_uv_plane.stride, width, height, width, height);
  if (ret != 0) {
    LOGF(WARNING) << "MJPGToNV12() failed with " << ret;
    return nullptr;
  }

  return GrallocFrameBuffer::Resize(*temp_buffer, size);
}
}  // namespace

FakeStream::FakeStream()
    : buffer_manager_(CameraBufferManager::GetInstance()),
      jpeg_compressor_(JpegCompressor::GetInstance(
          CameraHal::GetInstance().GetMojoManagerToken())) {}

FakeStream::~FakeStream() = default;

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// static
std::unique_ptr<FakeStream> FakeStream::Create(
    const android::CameraMetadata& static_metadata,
    Size size,
    android_pixel_format_t format,
    const FramesSpec& spec) {
  std::unique_ptr<FakeStream> fake_stream = std::visit(
      Overloaded{
          [&size](const FramesTestPatternSpec& spec)
              -> std::unique_ptr<FakeStream> {
            auto input_buffer = GenerateTestPattern(
                size, ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY);
            return base::WrapUnique(
                new StaticFakeStream(std::move(input_buffer)));
          },
          [&size](const FramesFileSpec& spec) -> std::unique_ptr<FakeStream> {
            auto extension = spec.path.Extension();
            if (extension == ".jpg" || extension == ".jpeg" ||
                extension == ".mjpg" || extension == ".mjpeg") {
              // TODO(pihsun): This only reads a single frame now, read and
              // convert the whole stream on fly.
              auto input_buffer = ReadMJPGFromFile(spec.path, size);
              return base::WrapUnique(
                  new StaticFakeStream(std::move(input_buffer)));
            } else if (extension == ".y4m") {
              return base::WrapUnique(new Y4mFakeStream(spec.path));
            } else {
              LOGF(WARNING) << "Unknown file extension: " << extension;
              return nullptr;
            }
          },
      },
      spec);
  if (fake_stream == nullptr ||
      !fake_stream->Initialize(static_metadata, size, format, spec)) {
    return nullptr;
  }
  return fake_stream;
}

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
  return true;
}

bool FakeStream::CopyBuffer(FrameBuffer& buffer,
                            buffer_handle_t output_buffer) {
  auto frame_buffer = GrallocFrameBuffer::Wrap(output_buffer, size_);
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

  auto mapped_buffer = buffer.Map();
  if (mapped_buffer == nullptr) {
    LOGF(WARNING) << "failed to map the fake stream buffer";
    return false;
  }

  auto mapped_frame_buffer = frame_buffer->Map();
  if (mapped_frame_buffer == nullptr) {
    LOGF(WARNING) << "failed to map the input buffer";
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

std::unique_ptr<GrallocFrameBuffer> FakeStream::ConvertBuffer(
    std::unique_ptr<GrallocFrameBuffer> buffer, android_pixel_format_t format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_BLOB: {
      auto output_buffer =
          GrallocFrameBuffer::Create(Size(jpeg_max_size_, 1), format);
      if (!output_buffer) {
        return nullptr;
      }

      uint32_t out_data_size;

      std::vector<uint8_t> app1;
      // TODO(pihsun): Fill thumbnail in app1.
      // TODO(pihsun): Should use android.jpeg.quality in request metadata for
      // JPEG quality. Cache the frame using default quality in request
      // template, and redo JPEG encoding when quality changes.
      bool success = jpeg_compressor_->CompressImageFromHandle(
          buffer->GetBufferHandle(), output_buffer->GetBufferHandle(),
          buffer->GetSize().width, buffer->GetSize().height, /*quality=*/90,
          app1.data(), app1.size(), &out_data_size);
      if (!success) {
        LOGF(WARNING) << "failed to encode JPEG";
        return nullptr;
      }

      auto mapped_buffer = output_buffer->Map();
      if (mapped_buffer == nullptr) {
        LOGF(WARNING) << "failed to map the buffer";
        return nullptr;
      }

      auto data = mapped_buffer->plane(0).addr;

      camera3_jpeg_blob_t blob = {
          .jpeg_blob_id = CAMERA3_JPEG_BLOB_ID,
          .jpeg_size = out_data_size,
      };

      CHECK(base::ClampAdd(out_data_size, sizeof(blob)) <= jpeg_max_size_);
      memcpy(data + jpeg_max_size_ - sizeof(blob), &blob, sizeof(blob));
      return output_buffer;
    }
    case HAL_PIXEL_FORMAT_YCBCR_420_888:
      return buffer;
    default:
      NOTIMPLEMENTED() << "format = " << format << " is not supported";
      return nullptr;
  }
}

StaticFakeStream::StaticFakeStream(std::unique_ptr<GrallocFrameBuffer> buffer)
    : buffer_(std::move(buffer)) {}

bool StaticFakeStream::FillBuffer(buffer_handle_t output_buffer) {
  return CopyBuffer(*buffer_, output_buffer);
}

bool StaticFakeStream::Initialize(
    const android::CameraMetadata& static_metadata,
    Size size,
    android_pixel_format_t format,
    const FramesSpec& spec) {
  if (!FakeStream::Initialize(static_metadata, size, format, spec)) {
    return false;
  }

  auto input_buffer = std::move(buffer_);
  if (input_buffer == nullptr) {
    LOGF(WARNING) << "Failed to generate input buffer";
    return false;
  }

  buffer_ = ConvertBuffer(std::move(input_buffer), format);
  if (buffer_ == nullptr) {
    LOGF(WARNING) << "Failed to convert input buffer to format " << format;
    return false;
  }

  return true;
}

}  // namespace cros
