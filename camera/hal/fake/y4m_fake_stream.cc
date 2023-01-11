/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// The Y4M parser is adapted from Chromium
// media/capture/video/file_video_capture_device.cc
//
// Reference of the y4m file format: https://linux.die.net/man/5/yuv4mpeg

#include "hal/fake/y4m_fake_stream.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_tokenizer.h>
#include <libyuv.h>
#include <linux/videodev2.h>

#include "hal/fake/frame_buffer/cpu_memory_frame_buffer.h"
#include "hal/fake/frame_buffer/gralloc_frame_buffer.h"

#include "cros-camera/common.h"

namespace cros {

namespace {
static const int kY4mHeaderMaxSize = 4096;

static const char kY4mFrameDelimiter[] = "FRAME";

// String length excludes the ending '\0';
static const size_t kY4mFrameDelimiterLength = sizeof(kY4mFrameDelimiter) - 1;

static const char kY4mHeaderMagic[] = "YUV4MPEG2";

std::unique_ptr<FrameBuffer> ConvertI420ToNv12(
    std::unique_ptr<FrameBuffer> buffer) {
  auto output_buffer = GrallocFrameBuffer::Create(
      buffer->GetSize(), HAL_PIXEL_FORMAT_YCBCR_420_888);

  if (output_buffer == nullptr) {
    LOGF(WARNING) << "Failed to allocate output buffer";
    return nullptr;
  }

  auto mapped_buffer = buffer->Map();
  if (mapped_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map input buffer";
    return nullptr;
  }

  auto y_plane = mapped_buffer->plane(0);
  auto u_plane = mapped_buffer->plane(1);
  auto v_plane = mapped_buffer->plane(2);

  auto mapped_output_buffer = output_buffer->Map();
  if (mapped_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map output buffer";
    return nullptr;
  }

  auto output_y_plane = mapped_output_buffer->plane(0);
  auto output_uv_plane = mapped_output_buffer->plane(1);

  int ret = libyuv::I420ToNV12(
      y_plane.addr, y_plane.stride, u_plane.addr, u_plane.stride, v_plane.addr,
      v_plane.stride, output_y_plane.addr, output_y_plane.stride,
      output_uv_plane.addr, output_uv_plane.stride, buffer->GetSize().width,
      buffer->GetSize().height);
  if (ret != 0) {
    LOGF(WARNING) << "I420ToNV12() failed with " << ret;
    return nullptr;
  }
  return output_buffer;
}

}  // namespace

Y4mFakeStream::Y4mFakeStream(const base::FilePath& file_path)
    : file_path_(file_path) {}

bool Y4mFakeStream::ParseY4mHeader(const std::string& header) {
  auto tokenizer = base::StringTokenizer(header, " ");
  if (!tokenizer.GetNext()) {
    LOGF(WARNING) << "Can't find header magic for y4m file";
    return false;
  }

  auto magic = tokenizer.token_piece();
  if (magic != kY4mHeaderMagic) {
    LOGF(WARNING) << "Wrong header magic for y4m file, expected "
                  << kY4mHeaderMagic << ", got " << magic;
    return false;
  }

  while (tokenizer.GetNext()) {
    auto token = tokenizer.token_piece();
    // Every token is supposed to have an identifier letter and a bunch of
    // information immediately after.
    if (token.size() <= 1) {
      LOGF(WARNING) << "Header tag with empty token found";
      return false;
    }

    char identifier = token[0];
    token.remove_prefix(1);

    switch (identifier) {
      case 'W':
        if (!base::StringToUint(token, &video_size_.width)) {
          LOGF(WARNING) << "Failed to parse width tag";
          return false;
        }
        break;
      case 'H':
        if (!base::StringToUint(token, &video_size_.height)) {
          LOGF(WARNING) << "Failed to parse height tag";
          return false;
        }
        break;
      case 'F': {
        // TODO(pihsun): Actually parse frame rate.
        break;
      }
      case 'I':
        // Only progressive (no interlacing) is supported.
        if (token != "p") {
          LOGF(WARNING) << "Interlacing " << token << " is not supported";
          return false;
        }
        break;
      case 'A':
        // Pixel aspect ratio ignored.
        if (token != "1:1") {
          LOGF(WARNING) << "Pixel aspect ratio " << token << " is ignored";
        }
        break;
      case 'C':
        if (!(token == "420" || token == "420jpeg" || token == "420mpeg2" ||
              token == "420paldv")) {
          // Only I420 is supported, and we fudge the variants.
          LOGF(WARNING) << "Only I420 is supported and format " << token
                        << " is not supported.";
          return false;
        }
        break;
      default:
        break;
    }
  }
  if (!video_size_.is_valid()) {
    LOGF(WARNING) << "Image size is missing in header";
    return false;
  }
  if (video_size_.width > kFrameMaxDimension ||
      video_size_.height > kFrameMaxDimension) {
    LOGF(WARNING) << "Image size too large: " << video_size_.ToString();
    return false;
  }
  return true;
}

bool Y4mFakeStream::Initialize(const android::CameraMetadata& static_metadata,
                               Size size,
                               android_pixel_format_t format,
                               const FramesSpec& spec) {
  if (!FakeStream::Initialize(static_metadata, size, format, spec)) {
    return false;
  }

  file_ = base::File(file_path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file_.IsValid()) {
    auto error = file_.ErrorToString(file_.GetLastFileError());
    LOGF(WARNING) << "Failed to open file " << file_path_ << ": " << error;
    return false;
  }

  std::string header(kY4mHeaderMaxSize, '\0');
  int ret = file_.ReadAtCurrentPos(header.data(), header.size());
  if (ret == -1) {
    auto error = file_.ErrorToString(file_.GetLastFileError());
    LOGF(WARNING) << "Failed to read header for file " << file_path_ << ": "
                  << error;
    return false;
  }

  // The header line ends with a '\n' (0x0A).
  const size_t header_end = header.find('\n');
  if (header_end == std::string::npos) {
    LOGF(WARNING) << "Y4M header end not found in the first "
                  << kY4mHeaderMaxSize << " bytes";
    return false;
  }
  header = header.substr(0, header_end);

  if (!ParseY4mHeader(header)) {
    LOGF(WARNING) << "Failed to parse Y4M header";
    return false;
  }

  // Skip the '\n'.
  first_frame_byte_index_ = header_end + 1;

  if (file_.Seek(base::File::Whence::FROM_BEGIN, first_frame_byte_index_) ==
      -1) {
    LOGF(WARNING) << "Failed to seek to first frame";
    return false;
  }

  return true;
}

std::unique_ptr<FrameBuffer> Y4mFakeStream::ReadNextFrameI420() {
  // Y4m stores frame in YU12 / I420 format.
  auto temp_buffer =
      CpuMemoryFrameBuffer::Create(video_size_, V4L2_PIX_FMT_YUV420);
  if (temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to create temporary buffer: "
                  << video_size_.ToString();
    exit(0);
    // return nullptr;
  }

  auto mapped_temp_buffer = temp_buffer->Map();
  if (mapped_temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map temporary buffer";
    return nullptr;
  }

  auto temp_y_plane = mapped_temp_buffer->plane(0);
  auto temp_u_plane = mapped_temp_buffer->plane(1);
  auto temp_v_plane = mapped_temp_buffer->plane(2);

  // Note that sizeof() includes the ending '\0', and we also want to read one
  // more byte for the newline character.
  std::string header(kY4mFrameDelimiterLength, '\0');
  if (!file_.ReadAtCurrentPosAndCheck(
          {reinterpret_cast<uint8_t*>(header.data()), header.size()})) {
    // End of file, rewind and try again.
    if (file_.Seek(base::File::Whence::FROM_BEGIN, first_frame_byte_index_) ==
        -1) {
      LOGF(WARNING) << "Failed to rewind to first frame";
      return nullptr;
    }
    if (!file_.ReadAtCurrentPosAndCheck(
            {reinterpret_cast<uint8_t*>(header.data()), header.size()})) {
      LOGF(WARNING) << "Failed to read frame header";
      return nullptr;
    }
  }
  if (header != kY4mFrameDelimiter) {
    LOGF(WARNING) << "Wrong frame header, expected " << kY4mFrameDelimiter
                  << ", got " << header;
    return nullptr;
  }

  // Read the rest of frame header until newline. We ignore all the tags in
  // the frame header, since the I tag can't exist because we forbid Im in file
  // header, and the X tag doesn't affect the parsing.
  // Note that we expect there be no extra tags here except the newline
  // character, so the "while" loop should run only one iteration most of the
  // time.
  uint8_t tags[1];
  bool ret;
  while ((ret = file_.ReadAtCurrentPosAndCheck(tags)) && tags[0] != '\n') {
  }
  if (!ret) {
    LOGF(WARNING) << "Failed to read frame header";
    return nullptr;
  }

  if (!file_.ReadAtCurrentPosAndCheck({temp_y_plane.addr, temp_y_plane.size})) {
    LOGF(WARNING) << "Failed to read frame y plane";
    return nullptr;
  }

  if (!file_.ReadAtCurrentPosAndCheck({temp_u_plane.addr, temp_u_plane.size})) {
    LOGF(WARNING) << "Failed to read frame u plane";
    return nullptr;
  }

  if (!file_.ReadAtCurrentPosAndCheck({temp_v_plane.addr, temp_v_plane.size})) {
    LOGF(WARNING) << "Failed to read frame v plane";
    return nullptr;
  }

  return temp_buffer;
}

bool Y4mFakeStream::FillBuffer(buffer_handle_t output_buffer) {
  auto temp_i420_buffer = ReadNextFrameI420();
  if (temp_i420_buffer == nullptr) {
    LOGF(WARNING) << "Failed to read next frame";
    return false;
  }

  auto temp_buffer = ConvertI420ToNv12(std::move(temp_i420_buffer));
  if (temp_buffer == nullptr) {
    LOGF(WARNING) << "Failed to convert i420 to nv12";
    return false;
  }

  auto buffer = GrallocFrameBuffer::Resize(*temp_buffer, size_);
  if (buffer == nullptr) {
    LOGF(WARNING) << "Failed to resize frame";
    return false;
  }

  auto converted_buffer = ConvertBuffer(std::move(buffer), format_);
  if (converted_buffer == nullptr) {
    LOGF(WARNING) << "Failed to convert buffer format";
    return false;
  }

  // TODO(pihsun): We could potentially save multiple copies here by directly
  // converting into the output buffer.
  return CopyBuffer(*converted_buffer, output_buffer);
}

}  // namespace cros
