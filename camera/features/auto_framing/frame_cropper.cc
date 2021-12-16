/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/frame_cropper.h"

#include <algorithm>

#include <sync/sync.h>

#include "common/reloadable_config_file.h"
#include "cros-camera/common.h"
#include "gpu/egl/egl_fence.h"

namespace cros {

namespace {

constexpr char kCropFilterStrength[] = "crop_filter_strength";
constexpr char kMaxZoomRatio[] = "max_zoom_ratio";
constexpr char kRoiFilterStrength[] = "roi_filter_strength";
constexpr char kTargetCropToRoiRatio[] = "target_crop_to_roi_ratio";

float IirFilter(float current_value, float new_value, float strength) {
  const float next_value =
      strength * current_value + (1 - strength) * new_value;
  return std::max(next_value, 0.0f);
}

// Used for approximate all PTZ speed of different frame rates to that of 30fps.
constexpr float kUnitTimeSlice = 33.33f;

float ElapsedTimeMs(base::TimeTicks since) {
  if (since == base::TimeTicks::Max()) {
    return kUnitTimeSlice;
  }
  return (base::TimeTicks::Now() - since).InMillisecondsF();
}

}  // namespace

FrameCropper::FrameCropper(
    const Options& options,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : options_(options), task_runner_(task_runner) {
  task_runner_->PostTask(FROM_HERE, base::BindOnce(&FrameCropper::SetUpPipeline,
                                                   base::Unretained(this)));
}

void FrameCropper::OnNewFaceRegions(int frame_number,
                                    const std::vector<Rect<float>>& faces) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (faces.empty()) {
    // TODO(jcliang): See if we want to zoom out to whole frame.
    return;
  }
  float min_x0 = 1.0f, min_y0 = 1.0f, max_x1 = 0.0f, max_y1 = 0.0f;
  for (const auto& f : faces) {
    min_x0 = std::min(f.left, min_x0);
    min_y0 = std::min(f.top, min_y0);
    max_x1 = std::max(f.right(), max_x1);
    max_y1 = std::max(f.bottom(), max_y1);
  }
  region_of_interest_ =
      Rect<float>(min_x0, min_y0, max_x1 - min_x0, max_y1 - min_y0);
  ComputeActiveCropRegion(frame_number);
}

void FrameCropper::OnNewRegionOfInterest(int frame_number,
                                         const Rect<float>& roi) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!roi.is_valid()) {
    // TODO(jcliang): See if we want to zoom out to whole frame.
    return;
  }
  if (!region_of_interest_.is_valid()) {
    region_of_interest_ = roi;
  } else {
    region_of_interest_.left = IirFilter(region_of_interest_.left, roi.left,
                                         options_.roi_filter_strength);
    region_of_interest_.top = IirFilter(region_of_interest_.top, roi.top,
                                        options_.roi_filter_strength);
    region_of_interest_.width = IirFilter(region_of_interest_.width, roi.width,
                                          options_.roi_filter_strength);
    region_of_interest_.height = IirFilter(
        region_of_interest_.height, roi.height, options_.roi_filter_strength);
  }
  ComputeActiveCropRegion(frame_number);
}

base::ScopedFD FrameCropper::CropBuffer(
    int frame_number,
    buffer_handle_t input_yuv,
    base::ScopedFD input_acquire_fence,
    buffer_handle_t output_yuv,
    base::Optional<Rect<float>> crop_override) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  Rect<float> crop;
  if (crop_override) {
    crop = *crop_override;
  } else {
    ComputeActiveCropRegion(frame_number);
    crop = active_crop_region_;
  }
  if (input_acquire_fence.is_valid()) {
    sync_wait(input_acquire_fence.get(), 300);
  }
  SharedImage input_image = SharedImage::CreateFromBuffer(
      input_yuv, Texture2D::Target::kTarget2D, true);
  SharedImage output_image = SharedImage::CreateFromBuffer(
      output_yuv, Texture2D::Target::kTarget2D, true);
  image_processor_->CropYuv(input_image.y_texture(), input_image.uv_texture(),
                            crop, y_intermediate_.texture(),
                            uv_intermediate_.texture());
  image_processor_->YUVToYUV(
      y_intermediate_.texture(), uv_intermediate_.texture(),
      output_image.y_texture(), output_image.uv_texture());
  EglFence fence;
  return fence.GetNativeFd();
}

void FrameCropper::ConvertToCropSpace(
    std::vector<Rect<float>>* rectangles) const {
  DCHECK(task_runner_->BelongsToCurrentThread());

  auto transform_x = [&](float in_x) -> float {
    float mapped_x = std::clamp(
        (in_x - active_crop_region_.left) / active_crop_region_.width, 0.0f,
        1.0f);
    return mapped_x;
  };
  auto transform_y = [&](float in_y) -> float {
    float mapped_y = std::clamp(
        (in_y - active_crop_region_.top) / active_crop_region_.height, 0.0f,
        1.0f);
    return mapped_y;
  };
  for (auto& r : *rectangles) {
    const float left = transform_x(r.left);
    const float top = transform_y(r.top);
    r = Rect<float>(left, top, transform_x(r.right()) - left,
                    transform_y(r.bottom()) - top);
  }
}

Rect<float> FrameCropper::GetActiveCropRegion() const {
  DCHECK(task_runner_->BelongsToCurrentThread());

  return active_crop_region_;
}

void FrameCropper::OnOptionsUpdated(const base::Value& json_values) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  LoadIfExist(json_values, kMaxZoomRatio, &options_.max_zoom_ratio);
  LoadIfExist(json_values, kTargetCropToRoiRatio,
              &options_.target_crop_to_roi_ratio);
  LoadIfExist(json_values, kRoiFilterStrength, &options_.roi_filter_strength);
  LoadIfExist(json_values, kCropFilterStrength, &options_.crop_filter_strength);
  VLOGF(1) << "FrameCropper options:"
           << " max_zoom_ratio" << options_.max_zoom_ratio
           << " target_crop_to_roi_ratio=" << options_.target_crop_to_roi_ratio
           << " roi_filter_strength=" << options_.roi_filter_strength
           << " crop_filter_strength=" << options_.crop_filter_strength;
}

void FrameCropper::SetUpPipeline() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!egl_context_) {
    egl_context_ = EglContext::GetSurfacelessContext();
    if (!egl_context_->IsValid()) {
      LOGF(ERROR) << "Failed to create EGL context";
      return;
    }
  }
  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make EGL context current";
    return;
  }
  image_processor_ = std::make_unique<GpuImageProcessor>();
  if (!image_processor_) {
    LOGF(ERROR) << "Failed to create GpuImageProcessor";
    return;
  }
  y_intermediate_ = SharedImage::CreateFromGpuTexture(
      GL_R8, options_.input_size.width, options_.input_size.height);
  uv_intermediate_ = SharedImage::CreateFromGpuTexture(
      GL_RG8, options_.input_size.width / 2, options_.input_size.height / 2);
  if (!y_intermediate_.texture().IsValid() ||
      !uv_intermediate_.texture().IsValid()) {
    LOGF(ERROR) << "Failed to create intermediate textures";
    return;
  }
}

void FrameCropper::ComputeActiveCropRegion(int frame_number) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  const float min_crop_size = 1.0f / options_.max_zoom_ratio;
  const float new_x_crop_size =
      std::clamp(region_of_interest_.width * options_.target_crop_to_roi_ratio,
                 min_crop_size, 1.0f);
  const float new_y_crop_size =
      std::clamp(region_of_interest_.height * options_.target_crop_to_roi_ratio,
                 min_crop_size, 1.0f);
  const float new_crop_size = std::max(new_x_crop_size, new_y_crop_size);
  Rect<float> new_crop;
  new_crop.width = new_crop_size;
  new_crop.height = new_crop_size;

  const float roi_x_mid =
      region_of_interest_.left + (region_of_interest_.width / 2);
  const float roi_y_mid =
      region_of_interest_.top + (region_of_interest_.height / 2);
  new_crop.left =
      std::clamp(roi_x_mid - (new_crop.width / 2), 0.0f, 1.0f - new_crop.width);
  new_crop.top = std::clamp(roi_y_mid - (new_crop.height / 2), 0.0f,
                            1.0f - new_crop.height);

  const float normalized_crop_strength =
      std::powf(options_.crop_filter_strength,
                ElapsedTimeMs(timestamp_) / kUnitTimeSlice);
  active_crop_region_.left = IirFilter(active_crop_region_.left, new_crop.left,
                                       normalized_crop_strength);
  active_crop_region_.top = IirFilter(active_crop_region_.top, new_crop.top,
                                      normalized_crop_strength);
  active_crop_region_.width = IirFilter(
      active_crop_region_.width, new_crop.width, normalized_crop_strength);
  active_crop_region_.height = IirFilter(
      active_crop_region_.height, new_crop.height, normalized_crop_strength);
  timestamp_ = base::TimeTicks::Now();

  if (VLOG_IS_ON(2)) {
    DVLOGFID(2, frame_number) << "region_of_interest=" << region_of_interest_;
    DVLOGFID(2, frame_number) << "new_crop_region=" << new_crop;
    DVLOGFID(2, frame_number) << "active_crop_region=" << active_crop_region_;
  }
}

}  // namespace cros
