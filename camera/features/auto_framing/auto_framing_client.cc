/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/auto_framing_client.h"

#include <hardware/gralloc.h>
#include <libyuv.h>

#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "common/camera_hal3_helpers.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

// Estimated duration in frames that input buffer sent to the auto-framing
// engine should keep valid.
constexpr size_t kInputBufferCount = 10;
constexpr uint32_t kInputBufferUsage = GRALLOC_USAGE_HW_TEXTURE |
                                       GRALLOC_USAGE_SW_READ_OFTEN |
                                       GRALLOC_USAGE_SW_WRITE_OFTEN;

constexpr char kAutoFramingGraphConfigOverridePath[] =
    "/run/camera/auto_framing_subgraph.pbtxt";

}  // namespace

bool AutoFramingClient::SetUp(const Options& options) {
  base::AutoLock lock(lock_);

  AutoFramingCrOS::Options auto_framing_options = {
      .input_format = AutoFramingCrOS::ImageFormat::kGRAY8,
      .input_width = base::checked_cast<int>(options.input_size.width),
      .input_height = base::checked_cast<int>(options.input_size.height),
      .frame_rate = options.frame_rate,
      .target_aspect_ratio_x =
          base::checked_cast<int>(options.target_aspect_ratio_x),
      .target_aspect_ratio_y =
          base::checked_cast<int>(options.target_aspect_ratio_y),
  };
  std::string graph_config;
  std::string* graph_config_ptr = nullptr;
  if (base::ReadFileToString(
          base::FilePath(kAutoFramingGraphConfigOverridePath), &graph_config)) {
    graph_config_ptr = &graph_config;
  }
  auto_framing_ = AutoFramingCrOS::Create();
  if (!auto_framing_ || !auto_framing_->Initialize(auto_framing_options, this,
                                                   graph_config_ptr)) {
    LOGF(ERROR) << "Failed to initialize auto-framing engine";
    auto_framing_ = nullptr;
    return false;
  }

  // Allocate buffers for auto-framing engine inputs.
  // TODO(kamesan): Use a smaller size if detection works well.
  CameraBufferPool::Options buffer_pool_options = {
      .width = options.input_size.width,
      .height = options.input_size.height,
      .format = HAL_PIXEL_FORMAT_Y8,
      .usage = kInputBufferUsage,
      .max_num_buffers = kInputBufferCount,
  };
  buffer_pool_ = std::make_unique<CameraBufferPool>(buffer_pool_options);

  region_of_interest_ = std::nullopt;
  crop_window_ =
      GetCenteringFullCrop(options.input_size, options.target_aspect_ratio_x,
                           options.target_aspect_ratio_y);

  return true;
}

bool AutoFramingClient::ProcessFrame(int64_t timestamp,
                                     buffer_handle_t src_buffer) {
  base::AutoLock lock(lock_);

  if (!auto_framing_) {
    LOGF(ERROR) << "AutoFramingClient is not initialized";
    return false;
  }

  DCHECK_NE(buffer_pool_, nullptr);
  std::optional<CameraBufferPool::Buffer> dst_buffer =
      buffer_pool_->RequestBuffer();
  if (!dst_buffer) {
    LOGF(ERROR) << "Failed to allocate buffer for detection @" << timestamp;
    return false;
  }

  // TODO(kamesan): Use GPU to copy/scale the buffers.
  ScopedMapping src_mapping(src_buffer);
  const ScopedMapping& dst_mapping = dst_buffer->Map();
  DCHECK_EQ(src_mapping.width(), dst_mapping.width());
  DCHECK_EQ(src_mapping.height(), dst_mapping.height());
  libyuv::CopyPlane(src_mapping.plane(0).addr, src_mapping.plane(0).stride,
                    dst_mapping.plane(0).addr, dst_mapping.plane(0).stride,
                    dst_mapping.width(), dst_mapping.height());

  VLOGF(2) << "Process frame @" << timestamp;
  if (!auto_framing_->ProcessFrame(timestamp, dst_mapping.plane(0).addr,
                                   dst_mapping.plane(0).stride)) {
    LOGF(ERROR) << "Failed to process frame @" << timestamp;
    return false;
  }

  DCHECK_EQ(inflight_buffers_.count(timestamp), 0);
  inflight_buffers_.insert(std::make_pair(timestamp, *std::move(dst_buffer)));

  return true;
}

std::optional<Rect<uint32_t>> AutoFramingClient::TakeNewRegionOfInterest() {
  base::AutoLock lock(lock_);
  std::optional<Rect<uint32_t>> roi;
  roi.swap(region_of_interest_);
  return roi;
}

Rect<uint32_t> AutoFramingClient::GetCropWindow() {
  base::AutoLock lock(lock_);
  return crop_window_;
}

void AutoFramingClient::TearDown() {
  auto_framing_.reset();
  inflight_buffers_.clear();
  buffer_pool_.reset();
}

void AutoFramingClient::OnFrameProcessed(int64_t timestamp) {
  VLOGF(2) << "Release frame @" << timestamp;

  base::AutoLock lock(lock_);
  inflight_buffers_.erase(timestamp);
}

void AutoFramingClient::OnNewRegionOfInterest(
    int64_t timestamp, int x_min, int y_min, int x_max, int y_max) {
  VLOGF(2) << "ROI @" << timestamp << ": " << x_min << "," << y_min << ","
           << x_max << "," << y_max;

  base::AutoLock lock(lock_);
  region_of_interest_ =
      Rect<int>(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1)
          .AsRect<uint32_t>();
}

void AutoFramingClient::OnNewCropWindow(
    int64_t timestamp, int x_min, int y_min, int x_max, int y_max) {
  VLOGF(2) << "Crop window @" << timestamp << ": " << x_min << "," << y_min
           << "," << x_max << "," << y_max;

  base::AutoLock lock(lock_);
  crop_window_ = Rect<int>(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1)
                     .AsRect<uint32_t>();
}

void AutoFramingClient::OnNewAnnotatedFrame(int64_t timestamp,
                                            const uint8_t* data,
                                            int stride) {
  VLOGF(2) << "Annotated frame @" << timestamp;

  // TODO(kamesan): Draw annotated frame in debug mode.
}

}  // namespace cros
