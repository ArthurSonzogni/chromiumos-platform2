/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/memory/unsafe_shared_memory_region.h>
#include <base/synchronization/lock.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/common_types.h"
#include "cros-camera/export.h"
#include "cros-camera/face_detector_client_cros_wrapper.h"

namespace cros {

enum class FaceDetectResult {
  kDetectOk,
  kDetectError,
  kBufferError,
  kTransformError,
};

// This class encapsulates Google3 FaceSSD library.
class CROS_CAMERA_EXPORT FaceDetector {
 public:
  static std::unique_ptr<FaceDetector> Create();

  // Detects human faces. |buffer| should be in NV12 pixel format. The detected
  // results will be stored in |faces|. |human_sensing::CrosFace| includes a
  // bounding box and confidence information.
  //
  // Caller can iterate the vector as the pseudo code:
  //
  // for (const auto& face : faces) {
  //   // Bounding box of the detected face. (x1, y1) is top left corner and
  //   // (x2, y2) is bottom right corner.
  //   float x1 = face.bounding_box.x1, y1 = face.bounding_box.y1,
  //         x2 = face.bounding_box.x2, y2 = face.bounding_box.y2;
  //
  //   // Confidence of the detected face in range [0.0, 1.0]. High confidence
  //   // score corresponds to high likelihood that the detected region is human
  //   // face.
  //   float confidence = face.confidence;
  // }
  //
  // If |active_sensor_array_size| is specified, the coordinates of the bounding
  // boxes in |faces| will be mapped to the "pre-corrected" coordinate space
  // using |active_sensor_array_size| as the raw sensor area, matching the
  // requirement of Android HAL3 requirements. Otherwise, the coordinates of the
  // bounding boxes will be mapped to the dimension of |buffer|.
  FaceDetectResult Detect(
      buffer_handle_t buffer,
      std::vector<human_sensing::CrosFace>* faces,
      base::Optional<Size> active_sensor_array_size = base::nullopt);

  // For a given size |src| that's downscaled and/or cropped from |dst|, get the
  // transformation parameters that converts a coordinate (x, y) in
  // [0, src.width] x [0, src.height] to [0, dst.width] x [0, dst.height]:
  //
  //   x_dst = S * x_src + offset_x
  //   y_dst = S * y_src + offset_y
  //
  // Returns a float tuple (S, offset_x, offset_y).
  static base::Optional<std::tuple<float, float, float>> GetCoordinateTransform(
      const Size src, const Size dst);

 private:
  FaceDetector(
      std::unique_ptr<human_sensing::FaceDetectorClientCrosWrapper> wrapper);

  void PrepareBuffer(Size img_size);

  int ScaleImage(buffer_handle_t buffer, Size input_size, Size output_size);

  // Used to import gralloc buffer.
  CameraBufferManager* buffer_manager_;

  base::Lock lock_;
  std::vector<uint8_t> scaled_buffer_ GUARDED_BY(lock_);

  std::unique_ptr<human_sensing::FaceDetectorClientCrosWrapper> wrapper_;
};

std::string LandmarkTypeToString(human_sensing::Landmark::Type type);

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_
