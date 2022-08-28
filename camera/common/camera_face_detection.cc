/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/camera_face_detection.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/posix/safe_strerror.h>
#include <libyuv.h>

#include "common/tracing.h"
#include "cros-camera/common.h"
#include "cros-camera/tracing.h"

namespace cros {

// This class only supports gray type model. See go/facessd for more details.
const char kFaceModelPath[] =
    "/usr/share/cros-camera/ml_models/fssd_small_8bit_gray_4orient_v4.tflite";
const char kFaceAnchorPath[] =
    "/usr/share/cros-camera/ml_models/fssd_anchors_v4.pb";
const float kScoreThreshold = 0.5;
const int kImageSizeForDetection = 160;

// static
std::unique_ptr<FaceDetector> FaceDetector::Create() {
  if (!base::PathExists(base::FilePath(kFaceModelPath)) ||
      !base::PathExists(base::FilePath(kFaceAnchorPath))) {
    LOGF(ERROR) << "Cannot find face detection model file or anchor file";
    return nullptr;
  }

  auto wrapper =
      std::make_unique<human_sensing::FaceDetectorClientCrosWrapper>();
  if (!wrapper->Initialize(std::string(kFaceModelPath),
                           std::string(kFaceAnchorPath), kScoreThreshold)) {
    return nullptr;
  }
  return base::WrapUnique(new FaceDetector(std::move(wrapper)));
}

FaceDetector::FaceDetector(
    std::unique_ptr<human_sensing::FaceDetectorClientCrosWrapper> wrapper)
    : buffer_manager_(CameraBufferManager::GetInstance()),
      wrapper_(std::move(wrapper)) {}

FaceDetectResult FaceDetector::Detect(
    buffer_handle_t buffer,
    std::vector<human_sensing::CrosFace>* faces,
    std::optional<Size> active_sensor_array_size) {
  TRACE_COMMON(kCameraTraceKeyWidth, buffer_manager_->GetWidth(buffer),
               kCameraTraceKeyHeight, buffer_manager_->GetHeight(buffer));

  DCHECK(faces);
  ScopedMapping mapping(buffer);
  if (!mapping.is_valid()) {
    LOGF(ERROR) << "Failed to map buffer";
    return FaceDetectResult::kBufferError;
  }
  int input_stride = mapping.plane(0).stride;
  Size input_size = Size(buffer_manager_->GetWidth(buffer),
                         buffer_manager_->GetHeight(buffer));
  const uint8_t* buffer_addr = static_cast<uint8_t*>(mapping.plane(0).addr);

  return Detect(buffer_addr, input_stride, input_size, faces,
                active_sensor_array_size);
}

FaceDetectResult FaceDetector::Detect(
    const uint8_t* buffer_addr,
    int input_stride,
    Size input_size,
    std::vector<human_sensing::CrosFace>* faces,
    std::optional<Size> active_sensor_array_size) {
  CHECK(faces);
  CHECK(buffer_addr);
  base::AutoLock l(lock_);

  Size scaled_size =
      (input_size.width > input_size.height)
          ? Size(kImageSizeForDetection,
                 kImageSizeForDetection * input_size.height / input_size.width)
          : Size(kImageSizeForDetection * input_size.width / input_size.height,
                 kImageSizeForDetection);

  PrepareBuffer(scaled_size);

  libyuv::ScalePlane(buffer_addr, input_stride, input_size.width,
                     input_size.height, scaled_buffer_.data(),
                     scaled_size.width, scaled_size.width, scaled_size.height,
                     libyuv::FilterMode::kFilterNone);

  {
    TRACE_EVENT_BEGIN(kCameraTraceCategoryCommon, "FaceDetector::Detect::Run");
    if (!wrapper_->Detect(scaled_buffer_.data(), scaled_size.width,
                          scaled_size.height, faces)) {
      return FaceDetectResult::kDetectError;
    }
    TRACE_EVENT_END(kCameraTraceCategoryCommon, "num_faces", faces->size());
  }

  if (!faces->empty()) {
    float ratio = static_cast<float>(input_size.width) /
                  static_cast<float>(scaled_size.width);
    for (auto& f : *faces) {
      f.bounding_box.x1 *= ratio;
      f.bounding_box.y1 *= ratio;
      f.bounding_box.x2 *= ratio;
      f.bounding_box.y2 *= ratio;
      for (auto& l : f.landmarks) {
        l.x *= ratio;
        l.y *= ratio;
      }
    }
  }

  if (active_sensor_array_size) {
    std::optional<std::tuple<float, float, float>> transform =
        GetCoordinateTransform(input_size, *active_sensor_array_size);
    if (!transform) {
      return FaceDetectResult::kTransformError;
    }
    const float scale = std::get<0>(*transform);
    const float offset_x = std::get<1>(*transform);
    const float offset_y = std::get<2>(*transform);
    for (auto& f : *faces) {
      f.bounding_box.x1 = scale * f.bounding_box.x1 + offset_x;
      f.bounding_box.y1 = scale * f.bounding_box.y1 + offset_y;
      f.bounding_box.x2 = scale * f.bounding_box.x2 + offset_x;
      f.bounding_box.y2 = scale * f.bounding_box.y2 + offset_y;
      for (auto& l : f.landmarks) {
        l.x = scale * l.x + offset_x;
        l.y = scale * l.y + offset_y;
      }
    }
  }

  return FaceDetectResult::kDetectOk;
}

// static
std::optional<std::tuple<float, float, float>>
FaceDetector::GetCoordinateTransform(const Size src, const Size dst) {
  if (src.width > dst.width || src.height > dst.height) {
    return std::nullopt;
  }
  const float width_ratio = static_cast<float>(dst.width) / src.width;
  const float height_ratio = static_cast<float>(dst.height) / src.height;
  const float scaling = std::min(width_ratio, height_ratio);
  float offset_x = 0.0f, offset_y = 0.0f;
  if (width_ratio < height_ratio) {
    // |dst| has larger height than |src| * scaling.
    offset_y = (dst.height - (src.height * scaling)) / 2;
  } else {
    // |dst| has larger width than |src| * scaling.
    offset_x = (dst.width - (src.width * scaling)) / 2;
  }
  return std::make_tuple(scaling, offset_x, offset_y);
}

void FaceDetector::PrepareBuffer(Size img_size) {
  size_t new_size = img_size.width * img_size.height;
  if (new_size > scaled_buffer_.size()) {
    scaled_buffer_.resize(new_size);
  }
}

std::string LandmarkTypeToString(human_sensing::Landmark::Type type) {
  switch (type) {
    case human_sensing::Landmark::Type::kLeftEye:
      return "LeftEye";
    case human_sensing::Landmark::Type::kRightEye:
      return "RightEye";
    case human_sensing::Landmark::Type::kNoseTip:
      return "NoseTip";
    case human_sensing::Landmark::Type::kMouthCenter:
      return "MouthCenter";
    case human_sensing::Landmark::Type::kLeftEarTragion:
      return "LeftEarTragion";
    case human_sensing::Landmark::Type::kRightEarTragion:
      return "RightEarTragion";
    case human_sensing::Landmark::Type::kLandmarkUnknown:
      return "Unknown";
  }
  return base::StringPrintf("Undefined landmark type %d", type);
}

}  // namespace cros
