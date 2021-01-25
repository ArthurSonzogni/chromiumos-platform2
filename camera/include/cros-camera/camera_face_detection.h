/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_

#include <memory>
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
};

// This class encapsulates Google3 FaceSSD library.
class CROS_CAMERA_EXPORT FaceDetector {
 public:
  static std::unique_ptr<FaceDetector> Create();

  // Detects human faces. |buffer| should be NV12 pixel format. The detected
  // results will be stored in |faces|. |human_sensing::CrosFace| includes a
  // bounding box and confidence information. Caller can iterate the vector as
  // the pseudo code:
  // for (auto& face : faces) {
  //   // Bounding box of the detected face. Values are pixels.
  //   // (x1, y1) is top left corner. (x2, y2) is bottom right corner.
  //   float x1 = face.bounding_box.x1, y1 = face.bounding_box.y1,
  //       x2 = face.bounding_box.x2, y2 = face.bounding_box.y2;
  //
  //   // Confidence of the detected face.
  //   // confidence is in range [0.0, 1.0] and high score corresponds to high
  //   // likelihood that the detected region is human face.
  //   float confidence = face.confidence;
  // }
  FaceDetectResult Detect(buffer_handle_t buffer,
                          std::vector<human_sensing::CrosFace>* faces);

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

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_FACE_DETECTION_H_
