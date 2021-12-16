/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_FRAME_CROPPER_H_
#define CAMERA_FEATURES_AUTO_FRAMING_FRAME_CROPPER_H_

#include <memory>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/single_thread_task_runner.h>
#include <base/values.h>

#include "cros-camera/common_types.h"
#include "gpu/egl/egl_context.h"
#include "gpu/image_processor.h"
#include "gpu/shared_image.h"

namespace cros {

// FrameCropper takes a bounding rectangle for the region of interest (ROI) as
// input, temporal-filters the input to determine the intermediate crop regions,
// and produces the output cropped buffer.
class FrameCropper {
 public:
  struct Options {
    // The input buffer dimension in pixels.
    Size input_size;

    // The maximum allowed zoom ratio.
    float max_zoom_ratio = 2.0f;

    // The target ratio between the cropped region and the bounding rectangle of
    // the ROI. Smaller value would make the objects in the ROI look bigger
    // after framing.
    float target_crop_to_roi_ratio = 2.5f;

    // Temporal filter strength for the tracked ROI coordinates and size. Larger
    // filter strength gives more stable ROI coordinates.
    float roi_filter_strength = 0.97f;

    // Temporal filter strength for the crop region coordinates and size. Larger
    // filter strength gives slower, but often more granular, pan/tilt/zoom
    // transitions.
    float crop_filter_strength = 0.95f;
  };

  FrameCropper(const Options& options,
               scoped_refptr<base::SingleThreadTaskRunner> task_runner);
  ~FrameCropper() = default;

  FrameCropper(FrameCropper& other) = delete;
  FrameCropper& operator=(FrameCropper& other) = delete;

  void OnNewFaceRegions(int frame_number,
                        const std::vector<Rect<float>>& faces);
  void OnNewRegionOfInterest(int frame_number, const Rect<float>& roi);

  // Crops |input_yuv| into |output_yuv| with the active crop region, or with
  // |crop_override| if given.
  base::ScopedFD CropBuffer(
      int frame_number,
      buffer_handle_t input_yuv,
      base::ScopedFD input_acquire_fence,
      buffer_handle_t output_yuv,
      base::Optional<Rect<float>> crop_override = base::nullopt);

  // Translates the coordinates of the normalized rectangles |rectangles| in the
  // global active array space to normalized rectangles in the crop space.
  void ConvertToCropSpace(std::vector<Rect<float>>* rectangles) const;

  // Gets the active region, out of the full frame area, that needs to be
  // cropped to emulate PTZ.
  Rect<float> GetActiveCropRegion() const;

  void OnOptionsUpdated(const base::Value& json_values);

 private:
  void SetUpPipeline();
  void ComputeActiveCropRegion(int frame_number);

  Options options_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  Rect<float> region_of_interest_ = {0.0f, 0.0f, 1.0f, 1.0f};
  Rect<float> active_crop_region_ = {0.0f, 0.0f, 1.0f, 1.0f};
  base::TimeTicks timestamp_ = base::TimeTicks::Max();

  std::unique_ptr<EglContext> egl_context_;
  std::unique_ptr<GpuImageProcessor> image_processor_;
  SharedImage y_intermediate_;
  SharedImage uv_intermediate_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_FRAME_CROPPER_H_
