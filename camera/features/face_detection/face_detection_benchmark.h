// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_BENCHMARK_H_
#define CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_BENCHMARK_H_

#include <memory>
#include <string>
#include <vector>

#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/camera_face_detection.h"
#include "feature_benchmark/benchmark_runner.h"

namespace cros::tests {
class FaceDetectionBenchmark : public BenchmarkRunner {
 public:
  // |benchmark_config.test_case_config| should be of the following format.
  // {
  //  "fps": (int),
  //  "input_height": (int),
  //  "input_image_relative_path": (string),
  //  "input_width": (int),
  // }
  explicit FaceDetectionBenchmark(const BenchmarkConfig& benchmark_config,
                                  const base::FilePath& data_dir);

 protected:
  bool Initialize() final;
  void Run() final;

 private:
  struct FaceDetectionBenchmarkConfig {
    int input_width;
    int input_height;
    const std::string* input_image_relative_path;
  };
  FaceDetectionBenchmarkConfig config_;
  ScopedBufferHandle buffer_handle_;
  std::unique_ptr<FaceDetector> face_detector_;
  std::optional<Size> active_sensor_array_size_;
  std::vector<human_sensing::CrosFace> faces_;
};

}  // namespace cros::tests

#endif  // CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_BENCHMARK_H_
