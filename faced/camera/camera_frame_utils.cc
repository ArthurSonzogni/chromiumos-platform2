// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/camera_frame_utils.h"

#include <memory>
#include <string>

#include <base/strings/strcat.h>
#include <base/strings/string_piece.h>
#include <linux/videodev2.h>

#include "faced/camera/camera_client.h"
#include "faced/proto/face_service.pb.h"

namespace faced {
namespace {

// Stride is unused if the stride value is 0 or equal to width
bool IsStrideUnused(int stride, int width) {
  return stride == 0 || stride == width;
}

}  // namespace

std::string GetTightlyPackedPayload(int height,
                                    int width,
                                    const cros_cam_plane_t_& plane_y,
                                    const cros_cam_plane_t_& plane_uv) {
  // If the stride values are unused then no further processing needs to be
  // done.
  if (IsStrideUnused(plane_y.stride, width) &&
      IsStrideUnused(plane_uv.stride, width)) {
    return base::StrCat({
        base::StringPiece(reinterpret_cast<const char*>(plane_y.data),
                          plane_y.size),
        base::StringPiece(reinterpret_cast<const char*>(plane_uv.data),
                          plane_uv.size),
    });
  }

  // The expected size is equal to a full size luminance (Y) component of size
  // (height * width) with a 2x2 subsampled chroma (UV) component of size
  // (height + 1) / 2 * width
  int expected_size = height * width + (height + 1) / 2 * width;
  std::string payload = std::string(expected_size, 0);

  // First copy the luminance (Y) values
  // Keep track of pointers to the start of each source and destination row
  uint8_t* src_y = plane_y.data;
  char* dst = payload.data();

  // If the stride value is unused, then the luminance data is already tightly
  // packed.
  if (IsStrideUnused(plane_y.stride, width)) {
    memcpy(dst, src_y, height * width);
    dst += height * width;
  } else {
    for (int i = 0; i < height; i++) {
      memcpy(dst, src_y, width);
      src_y += plane_y.stride;
      dst += width;
    }
  }

  // Next copy the subsampled chroma (UV) values
  uint8_t* src_uv = plane_uv.data;

  // If the stride value is unused, then the chroma data is already tightly
  // packed.
  if (IsStrideUnused(plane_uv.stride, width)) {
    memcpy(dst, src_uv, (height + 1) / 2 * width);
  } else {
    for (int i = 0; i < (height + 1) / 2; i++) {
      memcpy(dst, src_uv, width);
      src_uv += plane_uv.stride;
      dst += width;
    }
  }

  return payload;
}

std::unique_ptr<eora::CameraFrame> CameraFrameProtoFromCrosFrame(
    const cros_cam_frame_t& frame) {
  auto camera_frame = std::make_unique<eora::CameraFrame>();
  camera_frame->set_height(frame.format.height);
  camera_frame->set_width(frame.format.width);

  switch (frame.format.fourcc) {
    case V4L2_PIX_FMT_NV12:
      camera_frame->set_type(eora::FrameType::YUV_NV12);
      camera_frame->set_payload(
          GetTightlyPackedPayload(frame.format.height, frame.format.width,
                                  frame.planes[0], frame.planes[1]));
      break;
    case V4L2_PIX_FMT_MJPEG:
      camera_frame->set_type(eora::FrameType::MJPG);
      camera_frame->set_payload(std::string(
          reinterpret_cast<char*>(frame.planes[0].data), frame.planes[0].size));
      break;
  }

  return camera_frame;
}

}  // namespace faced
