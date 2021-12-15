/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_FACE_TRACKER_H_
#define CAMERA_FEATURES_AUTO_FRAMING_FACE_TRACKER_H_

#include <vector>

#include <base/time/time.h>
#include <base/values.h>

#include "cros-camera/common_types.h"
#include "cros-camera/face_detector_client_cros_wrapper.h"

namespace cros {

// FaceTracker takes a set of face data produced by FaceDetector as input,
// filters the input, and produces the bounding rectangle that encloses the
// filtered input.
class FaceTracker {
 public:
  struct Options {
    // The dimension of the active sensory array in pixels. Used for normalizing
    // the input face coordinates.
    Size active_array_dimension;

    // The dimension of the active stream that will be cropped. Used for
    // translating the ROI coordinates in the active array space.
    Size active_stream_dimension;

    // The threshold in ms for including a newly detected face for tracking.
    int face_phase_in_threshold_ms = 3000;

    // The threshold in ms for excluding a face that's no longer detected for
    // tracking.
    int face_phase_out_threshold_ms = 2000;

    // The angle range [|pan_angle_range|, -|pan_angle_range|] in degrees used
    // to determine if a face is looking at the camera.
    float pan_angle_range = 30.0f;
  };

  explicit FaceTracker(const Options& options);
  ~FaceTracker() = default;

  FaceTracker(FaceTracker& other) = delete;
  FaceTracker& operator=(FaceTracker& other) = delete;

  // Callback for when new face data are ready.
  void OnNewFaceData(const std::vector<human_sensing::CrosFace>& faces);

  // The all the rectangles of all the detected faces.
  std::vector<Rect<float>> GetActiveFaceRectangles() const;

  // Gets the rectangle than encloses all the detected faces. Returns a
  // normalized rectangle in [0.0, 1.0] x [0.0, 1.0] with respect to the active
  // stream dimension.
  Rect<float> GetActiveBoundingRectangleOnActiveStream() const;

  void OnOptionsUpdated(const base::Value& json_values);

 private:
  struct FaceState {
    Rect<float> normalized_bounding_box = {0.0f, 0.0f, 0.0f, 0.0f};
    base::TimeTicks first_detected_ticks;
    base::TimeTicks last_detected_ticks;
    bool has_attention = false;
  };

  Options options_;
  std::vector<FaceState> faces_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_FACE_TRACKER_H_
