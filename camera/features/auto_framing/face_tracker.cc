/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/face_tracker.h"

#include <algorithm>

#include "common/reloadable_config_file.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kFacePhaseInThresholdMs[] = "face_phase_in_threshold_ms";
constexpr char kFacePhaseOutThresholdMs[] = "face_phase_out_threshold_ms";
constexpr char kPanAngleRange[] = "pan_angle_range";

int ElapsedTimeMs(base::TimeTicks ticks) {
  return (base::TimeTicks::Now() - ticks).InMilliseconds();
}

}  // namespace

FaceTracker::FaceTracker(const Options& options) : options_(options) {}

void FaceTracker::OnNewFaceData(
    const std::vector<human_sensing::CrosFace>& faces) {
  // Given |f1| and |f2| from two different (usually consecutive) frames, treat
  // the two rectangles as the same face if their position delta is less than
  // kFaceDistanceThresholdSquare.
  //
  // This is just a heuristic and is not accurate in some corner cases, but we
  // don't have face tracking.
  auto is_same_face = [&](const Rect<float>& f1,
                          const Rect<float>& f2) -> bool {
    const float center_f1_x = f1.left + f1.width / 2;
    const float center_f1_y = f1.top + f1.height / 2;
    const float center_f2_x = f2.left + f2.width / 2;
    const float center_f2_y = f2.top + f2.height / 2;
    constexpr float kFaceDistanceThresholdSquare = 0.1 * 0.1;
    const float dist_square = std::pow(center_f1_x - center_f2_x, 2.0f) +
                              std::pow(center_f1_y - center_f2_y, 2.0f);
    return dist_square < kFaceDistanceThresholdSquare;
  };

  for (const auto& f : faces) {
    FaceState s = {
        .normalized_bounding_box = Rect<float>(
            f.bounding_box.x1 / options_.active_array_dimension.width,
            f.bounding_box.y1 / options_.active_array_dimension.height,
            (f.bounding_box.x2 - f.bounding_box.x1) /
                options_.active_array_dimension.width,
            (f.bounding_box.y2 - f.bounding_box.y1) /
                options_.active_array_dimension.height),
        .last_detected_ticks = base::TimeTicks::Now(),
        .has_attention = std::fabs(f.pan_angle) < options_.pan_angle_range};

    bool found_matching_face = false;
    for (auto& known_face : faces_) {
      if (is_same_face(s.normalized_bounding_box,
                       known_face.normalized_bounding_box)) {
        found_matching_face = true;
        s.first_detected_ticks = known_face.first_detected_ticks;
        known_face = s;
        break;
      }
    }
    if (!found_matching_face) {
      s.first_detected_ticks = base::TimeTicks::Now();
      faces_.push_back(s);
    }
  }

  // Flush expired face states.
  for (auto it = faces_.begin(); it != faces_.end();) {
    if (ElapsedTimeMs(it->last_detected_ticks) >
        options_.face_phase_out_threshold_ms) {
      it = faces_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<Rect<float>> FaceTracker::GetActiveFaceRectangles() const {
  std::vector<Rect<float>> face_rectangles;
  face_rectangles.reserve(faces_.size());
  for (const auto& f : faces_) {
    if (f.has_attention && ElapsedTimeMs(f.first_detected_ticks) >
                               options_.face_phase_in_threshold_ms) {
      face_rectangles.emplace_back(f.normalized_bounding_box);
    }
  }
  return face_rectangles;
}

Rect<float> FaceTracker::GetActiveBoundingRectangleOnActiveStream() const {
  std::vector<Rect<float>> faces = GetActiveFaceRectangles();
  if (faces.empty()) {
    return Rect<float>();
  }
  float min_x0 = 1.0f, min_y0 = 1.0f, max_x1 = 0.0f, max_y1 = 0.0f;
  for (const auto& f : faces) {
    min_x0 = std::min(f.left, min_x0);
    min_y0 = std::min(f.top, min_y0);
    max_x1 = std::max(f.right(), max_x1);
    max_y1 = std::max(f.bottom(), max_y1);
  }
  Rect<float> bounding_rect(min_x0, min_y0, max_x1 - min_x0, max_y1 - min_y0);
  VLOGF(2) << "Active bounding rect w.r.t active array: " << bounding_rect;

  // Transform the normalized rectangle in the active sensor array space to the
  // active stream space.
  const float active_array_aspect_ratio =
      static_cast<float>(options_.active_array_dimension.width) /
      static_cast<float>(options_.active_array_dimension.height);
  const float active_stream_aspect_ratio =
      static_cast<float>(options_.active_stream_dimension.width) /
      static_cast<float>(options_.active_stream_dimension.height);
  if (active_array_aspect_ratio < active_stream_aspect_ratio) {
    // The active stream is cropped into letterbox with smaller height than the
    // active sensor array. Adjust the y coordinates accordingly.
    const float height_ratio =
        active_array_aspect_ratio / active_stream_aspect_ratio;
    bounding_rect.height = std::min(bounding_rect.height / height_ratio, 1.0f);
    const float y_offset = (1.0f - height_ratio) / 2;
    bounding_rect.top =
        std::max(bounding_rect.top - y_offset, 0.0f) / height_ratio;
  } else {
    // The active stream is cropped into pillarbox with smaller width than the
    // active sensor array. Adjust the x coordinates accordingly.
    const float width_ratio =
        active_stream_aspect_ratio / active_array_aspect_ratio;
    bounding_rect.width = std::min(bounding_rect.width / width_ratio, 1.0f);
    const float x_offset = (1.0f - width_ratio) / 2;
    bounding_rect.left =
        std::max(bounding_rect.left - x_offset, 0.0f) / width_ratio;
  }
  VLOGF(2) << "Active bounding rect w.r.t active stream: " << bounding_rect;

  return bounding_rect;
}

void FaceTracker::OnOptionsUpdated(const base::Value& json_values) {
  LoadIfExist(json_values, kFacePhaseInThresholdMs,
              &options_.face_phase_in_threshold_ms);
  LoadIfExist(json_values, kFacePhaseOutThresholdMs,
              &options_.face_phase_out_threshold_ms);
  LoadIfExist(json_values, kPanAngleRange, &options_.pan_angle_range);
  VLOGF(1) << "FaceTracker options:"
           << " face_phase_in_threshold_ms"
           << options_.face_phase_in_threshold_ms
           << " face_phase_out_threshold_ms="
           << options_.face_phase_out_threshold_ms
           << " pan_angle_range=" << options_.pan_angle_range;
}

}  // namespace cros
