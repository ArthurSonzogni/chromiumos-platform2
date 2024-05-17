/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/portrait_mode/portrait_mode_effect.h"

#include <drm_fourcc.h>
#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/memory/unsafe_shared_memory_region.h>
#include <base/numerics/safe_conversions.h>
#include <base/posix/eintr_wrapper.h>
#include <base/process/launch.h>
#include <base/time/time.h>
#include <base/values.h>
#include <libyuv.h>
#include <libyuv/convert_argb.h>
#include <system/camera_metadata.h>

#include "common/resizable_cpu_buffer.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/portrait_cros_wrapper.h"

namespace cros {

namespace {

constexpr int kPortraitProcessorTimeoutMs = 15000;

}  // namespace

PortraitModeEffect::PortraitModeEffect()
    : buffer_manager_(CameraBufferManager::GetInstance()),
      thread_("PortraitModeEffectThread") {
  CHECK(thread_.Start());
}

PortraitModeEffect::~PortraitModeEffect() {
  thread_.Stop();
}

void PortraitModeEffect::Initialize() {
  thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&PortraitModeEffect::InitializeAsync,
                                base::Unretained(this)));
}

int32_t PortraitModeEffect::ProcessRequest(
    buffer_handle_t input_buffer,
    uint32_t orientation,
    mojom::PortraitModeSegResult* segmentation_result,
    buffer_handle_t output_buffer) {
  if (!input_buffer || !output_buffer) {
    return -EINVAL;
  }

  int result = 0;
  base::ScopedClosureRunner result_metadata_runner(
      base::BindOnce(&PortraitModeEffect::UpdateSegmentationResult,
                     base::Unretained(this), segmentation_result, &result));
  auto task_completed = Future<int32_t>::Create(nullptr);
  thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&PortraitModeEffect::ProcessRequestAsync,
                     base::Unretained(this), input_buffer, output_buffer,
                     base::checked_cast<int>(orientation),
                     GetFutureCallback(task_completed)));
  if (!task_completed->Wait(kPortraitProcessorTimeoutMs)) {
    result = -ETIMEDOUT;
    return result;
  }
  result = task_completed->Get();
  return (result == -ECANCELED) ? 0 : result;
}

void PortraitModeEffect::UpdateSegmentationResult(
    mojom::PortraitModeSegResult* segmentation_result, const int* result) {
  *segmentation_result =
      (*result == 0)            ? mojom::PortraitModeSegResult::kSuccess
      : (*result == -ETIMEDOUT) ? mojom::PortraitModeSegResult::kTimeout
      : (*result == -ECANCELED) ? mojom::PortraitModeSegResult::kNoFaces
                                : mojom::PortraitModeSegResult::kFailure;
}

int PortraitModeEffect::ConvertYUVToRGB(const ScopedMapping& mapping,
                                        void* rgb_buf_addr,
                                        uint32_t rgb_buf_stride) {
  switch (mapping.v4l2_format()) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      if (libyuv::NV12ToRGB24(mapping.plane(0).addr, mapping.plane(0).stride,
                              mapping.plane(1).addr, mapping.plane(1).stride,
                              static_cast<uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from NV12 to RGB";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      if (libyuv::NV21ToRGB24(mapping.plane(0).addr, mapping.plane(0).stride,
                              mapping.plane(1).addr, mapping.plane(1).stride,
                              static_cast<uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from NV21 to RGB";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
      if (libyuv::I420ToRGB24(mapping.plane(0).addr, mapping.plane(0).stride,
                              mapping.plane(1).addr, mapping.plane(1).stride,
                              mapping.plane(2).addr, mapping.plane(2).stride,
                              static_cast<uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from YUV420 to RGB";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      if (libyuv::I420ToRGB24(mapping.plane(0).addr, mapping.plane(0).stride,
                              mapping.plane(2).addr, mapping.plane(2).stride,
                              mapping.plane(1).addr, mapping.plane(1).stride,
                              static_cast<uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from YVU420 to RGB";
        return -EINVAL;
      }
      break;
    default:
      LOGF(ERROR) << "Unsupported format "
                  << FormatToString(mapping.v4l2_format());
      return -EINVAL;
  }
  return 0;
}

int PortraitModeEffect::ConvertRGBToYUV(void* rgb_buf_addr,
                                        uint32_t rgb_buf_stride,
                                        const ScopedMapping& mapping) {
  auto convert_rgb_to_nv = [](const uint8_t* rgb_addr,
                              const ScopedMapping& mapping) {
    // TODO(hywu): convert RGB to NV12/NV21 directly
    auto div_round_up = [](uint32_t n, uint32_t d) {
      return ((n + d - 1) / d);
    };
    const uint32_t kRGBNumOfChannels = 3;
    uint32_t width = mapping.width();
    uint32_t height = mapping.height();
    uint32_t ystride = width;
    uint32_t cstride = div_round_up(width, 2);
    uint32_t total_size =
        width * height + cstride * div_round_up(height, 2) * 2;
    uint32_t uv_plane_size = cstride * div_round_up(height, 2);
    auto i420_y = std::make_unique<uint8_t[]>(total_size);
    uint8_t* i420_cb = i420_y.get() + width * height;
    uint8_t* i420_cr = i420_cb + uv_plane_size;
    if (libyuv::RGB24ToI420(static_cast<const uint8_t*>(rgb_addr),
                            width * kRGBNumOfChannels, i420_y.get(), ystride,
                            i420_cb, cstride, i420_cr, cstride, width,
                            height) != 0) {
      LOGF(ERROR) << "Failed to convert from RGB to I420";
      return -EINVAL;
    }
    if (mapping.v4l2_format() == V4L2_PIX_FMT_NV12) {
      if (libyuv::I420ToNV12(i420_y.get(), ystride, i420_cb, cstride, i420_cr,
                             cstride, mapping.plane(0).addr,
                             mapping.plane(0).stride, mapping.plane(1).addr,
                             mapping.plane(1).stride, width, height) != 0) {
        LOGF(ERROR) << "Failed to convert from I420 to NV12";
        return -EINVAL;
      }
    } else if (mapping.v4l2_format() == V4L2_PIX_FMT_NV21) {
      if (libyuv::I420ToNV21(i420_y.get(), ystride, i420_cb, cstride, i420_cr,
                             cstride, mapping.plane(0).addr,
                             mapping.plane(0).stride, mapping.plane(1).addr,
                             mapping.plane(1).stride, width, height) != 0) {
        LOGF(ERROR) << "Failed to convert from I420 to NV21";
        return -EINVAL;
      }
    } else {
      return -EINVAL;
    }
    return 0;
  };
  switch (mapping.v4l2_format()) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      if (convert_rgb_to_nv(static_cast<const uint8_t*>(rgb_buf_addr),
                            mapping) != 0) {
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
      if (libyuv::RGB24ToI420(static_cast<const uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.plane(0).addr,
                              mapping.plane(0).stride, mapping.plane(1).addr,
                              mapping.plane(1).stride, mapping.plane(2).addr,
                              mapping.plane(2).stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from RGB to YUV420";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      if (libyuv::RGB24ToI420(static_cast<const uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, mapping.plane(0).addr,
                              mapping.plane(0).stride, mapping.plane(2).addr,
                              mapping.plane(2).stride, mapping.plane(1).addr,
                              mapping.plane(1).stride, mapping.width(),
                              mapping.height()) != 0) {
        LOGF(ERROR) << "Failed to convert from RGB to YVU420";
        return -EINVAL;
      }
      break;
    default:
      LOGF(ERROR) << "Unsupported format "
                  << FormatToString(mapping.v4l2_format());
      return -EINVAL;
  }
  return 0;
}

void PortraitModeEffect::InitializeAsync() {
  CHECK(thread_.task_runner()->BelongsToCurrentThread());
  if (!portrait_processor_.Init()) {
    LOGF(ERROR) << "Failed to initialize portrait processor";
    return;
  }
}

void PortraitModeEffect::ProcessRequestAsync(
    buffer_handle_t input_buffer,
    buffer_handle_t output_buffer,
    int orientation,
    base::OnceCallback<void(int32_t)> task_completed_callback) {
  CHECK(thread_.task_runner()->BelongsToCurrentThread());
  CHECK(input_buffer);
  CHECK(output_buffer);

  ScopedMapping input_mapping(input_buffer);
  ScopedMapping output_mapping(output_buffer);
  uint32_t width = input_mapping.width();
  uint32_t height = input_mapping.height();
  uint32_t v4l2_format = input_mapping.v4l2_format();
  CHECK_EQ(output_mapping.width(), width);
  CHECK_EQ(output_mapping.height(), height);
  CHECK_EQ(output_mapping.v4l2_format(), v4l2_format);

  const uint32_t kRGBNumOfChannels = 3;

  ResizableCpuBuffer input_rgb_buffer, output_rgb_buffer;
  input_rgb_buffer.SetFormat(width, height, DRM_FORMAT_RGB888);
  output_rgb_buffer.SetFormat(width, height, DRM_FORMAT_RGB888);

  uint32_t rgb_buf_stride = width * kRGBNumOfChannels;

  int result = ConvertYUVToRGB(input_mapping, input_rgb_buffer.plane(0).addr,
                               rgb_buf_stride);
  if (result != 0) {
    LOGF(ERROR) << "Failed to convert from YUV to RGB";
    std::move(task_completed_callback).Run(result);
    return;
  }

  const creative_camera::PortraitCrosWrapper::Request portrait_request{
      .width = base::checked_cast<int>(width),
      .height = base::checked_cast<int>(height),
      .orientation = orientation,
  };

  LOGF(INFO) << "Starting portrait processing";
  if (!portrait_processor_.Process(req_id_++, portrait_request,
                                   input_rgb_buffer.plane(0).addr,
                                   output_rgb_buffer.plane(0).addr)) {
    // We process portrait images using Google3 portrait library. Not
    // processing cases is primarily due to no human face being detected.
    // We assume the failure here is not containing a clear face. Returns 0
    // here with the status set in the vendor tag by |result_metadata_runner|
    LOGF(WARNING) << "Portrait processor failed with no human face detected.";
    std::move(task_completed_callback).Run(-ECANCELED);
    return;
  }
  LOGF(INFO) << "Portrait processing finished, result: " << result;

  result = ConvertRGBToYUV(output_rgb_buffer.plane(0).addr, rgb_buf_stride,
                           output_mapping);
  if (result != 0) {
    LOGF(ERROR) << "Failed to convert from RGB to YUV";
  }
  std::move(task_completed_callback).Run(result);
}

}  // namespace cros
