/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_FRAMER_H_
#define CAMERA_FEATURES_AUTO_FRAMING_FRAMER_H_

#include <memory>
#include <optional>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <base/values.h>

#include "cros-camera/common_types.h"
#include "gpu/egl/egl_context.h"
#include "gpu/image_processor.h"
#include "gpu/shared_image.h"

namespace cros {

// Framer takes a bounding rectangle for the region of interest (ROI) as
// input, and temporal-filters the input to determine the intermediate crop
// regions. The user of this class has to make sure they synchronize the access
// to the methods.
class Framer {
 public:
  struct Options {
    // The input buffer dimension in pixels.
    Size input_size;

    // The target aspect ratio of the cropped region.
    uint32_t target_aspect_ratio_x = 16;
    uint32_t target_aspect_ratio_y = 9;

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

  explicit Framer(const Options& options);
  ~Framer() = default;

  Framer(const Framer& other) = delete;
  Framer& operator=(const Framer& other) = delete;

  void OnNewFaceRegions(int frame_number,
                        const std::vector<Rect<float>>& faces);
  void OnNewRegionOfInterest(int frame_number, const Rect<float>& roi);

  // Computes and gets the active region, out of the full frame area, that needs
  // to be cropped to emulate PTZ.
  Rect<float> ComputeActiveCropRegion(int frame_number);

  void OnOptionsUpdated(const base::Value& json_values);

 private:
  Options options_;

  Rect<float> region_of_interest_ = {0.0f, 0.0f, 1.0f, 1.0f};
  Rect<float> active_crop_region_ = {0.0f, 0.0f, 1.0f, 1.0f};
  base::TimeTicks timestamp_ = base::TimeTicks::Max();
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_FRAMER_H_
