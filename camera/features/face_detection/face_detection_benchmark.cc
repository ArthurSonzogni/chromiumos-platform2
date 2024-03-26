// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/face_detection/face_detection_benchmark.h"

#include <optional>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "common/test_support/test_image.h"
#include "cros-camera/camera_face_detection.h"
#include "cros-camera/common.h"
#include "feature_benchmark/benchmark_runner.h"

namespace cros::tests {

namespace {

struct StreamConfig {
  uint32_t width;
  uint32_t height;
  int format;
  uint32_t usage;
};

void LoadImageToBuffer(const base::FilePath& test_image_file,
                       StreamConfig& stream_config,
                       ScopedBufferHandle& buffer) {
  auto test_image = tests::TestImage::Create(test_image_file);
  if (!test_image) {
    LOGF(FATAL) << "Failed to load test image from " << test_image_file;
  }

  if (test_image->face_rectangles().size() != 1) {
    test_image.reset();
    LOGF(FATAL) << "Expected there's one face in the test image (metadata)";
  }

  const float test_image_aspect_ratio =
      static_cast<float>(test_image->width()) /
      static_cast<float>(test_image->height());
  const float stream_aspect_ratio = static_cast<float>(stream_config.width) /
                                    static_cast<float>(stream_config.height);
  int cropped_width = test_image->width(),
      cropped_height = test_image->height();
  if (test_image_aspect_ratio >= stream_aspect_ratio) {
    cropped_width =
        static_cast<int>(test_image->height() * stream_aspect_ratio + 0.5f);
  } else if (test_image_aspect_ratio < stream_aspect_ratio) {
    cropped_height =
        static_cast<int>(test_image->width() / stream_aspect_ratio + 0.5f);
  }

  const Rect<uint32_t> crop(
      static_cast<uint32_t>((test_image->width() - cropped_width) / 2),
      static_cast<uint32_t>((test_image->height() - cropped_height) / 2),
      static_cast<uint32_t>(cropped_width),
      static_cast<uint32_t>(cropped_height));
  CHECK(tests::WriteTestImageToBuffer(*test_image, *buffer, crop));
}

}  // namespace

FaceDetectionBenchmark::FaceDetectionBenchmark(
    const BenchmarkConfig& benchmark_config, const base::FilePath& data_dir)
    : BenchmarkRunner(data_dir) {
  constexpr char kInputWidthKey[] = "input_width";
  constexpr char kInputHeightKey[] = "input_width";
  constexpr char kInputImageRelativePathKey[] = "input_image_relative_path";
  std::optional<int> input_width =
      benchmark_config.test_case_config().FindInt(kInputWidthKey);
  CHECK(input_width.has_value());
  config_.input_width = *input_width;
  std::optional<int> input_height =
      benchmark_config.test_case_config().FindInt(kInputHeightKey);
  CHECK(input_height.has_value());
  config_.input_height = *input_height;
  config_.input_image_relative_path =
      benchmark_config.test_case_config().FindString(
          kInputImageRelativePathKey);
  CHECK(config_.input_image_relative_path);
}

bool FaceDetectionBenchmark::Initialize() {
  StreamConfig stream_config = {
      .width = static_cast<uint32_t>(config_.input_width),
      .height = static_cast<uint32_t>(config_.input_height),
      .format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
      .usage = 0,
  };
  buffer_handle_ = CameraBufferManager::AllocateScopedBuffer(
      stream_config.width, stream_config.height, stream_config.format,
      stream_config.usage);
  CHECK(buffer_handle_);

  LoadImageToBuffer(data_dir().Append(*config_.input_image_relative_path),
                    stream_config, buffer_handle_);
  face_detector_ = FaceDetector::Create();
  active_sensor_array_size_ = Size(stream_config.width, stream_config.height);
  return true;
}

void FaceDetectionBenchmark::Run() {
  faces_.clear();
  FaceDetectResult result = face_detector_->Detect(
      *buffer_handle_.get(), &faces_, active_sensor_array_size_);
  CHECK(result == FaceDetectResult::kDetectOk);
}

}  // namespace cros::tests
