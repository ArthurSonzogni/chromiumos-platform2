/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/camera_face_detection.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/posix/safe_strerror.h>
#include <libyuv.h>

#include "cros-camera/common.h"

namespace cros {

// This class only supports gray type model. See go/facessd for more details.
const char kFaceModelPath[] =
    "/opt/google/cros-camera/ml_models/fssd_small_8bit_gray_4orient_v4.tflite";
const char kFaceAnchorPath[] =
    "/opt/google/cros-camera/ml_models/fssd_anchors_v4.pb";
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
    base::Optional<Size> active_sensor_array_size) {
  DCHECK(faces);
  base::AutoLock l(lock_);
  Size input_size = Size(buffer_manager_->GetWidth(buffer),
                         buffer_manager_->GetHeight(buffer));

  Size scaled_size =
      (input_size.width > input_size.height)
          ? Size(kImageSizeForDetection,
                 kImageSizeForDetection * input_size.height / input_size.width)
          : Size(kImageSizeForDetection * input_size.width / input_size.height,
                 kImageSizeForDetection);

  PrepareBuffer(scaled_size);

  if (ScaleImage(buffer, input_size, scaled_size) != 0) {
    return FaceDetectResult::kBufferError;
  }

  if (!wrapper_->Detect(scaled_buffer_.data(), scaled_size.width,
                        scaled_size.height, faces)) {
    return FaceDetectResult::kDetectError;
  }

  if (!faces->empty()) {
    float ratio = static_cast<float>(input_size.width) /
                  static_cast<float>(scaled_size.width);
    for (auto& f : *faces) {
      f.bounding_box.x1 *= ratio;
      f.bounding_box.y1 *= ratio;
      f.bounding_box.x2 *= ratio;
      f.bounding_box.y2 *= ratio;
    }
  }

  if (active_sensor_array_size) {
    base::Optional<std::tuple<float, float, float>> transform =
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
    }
  }

  return FaceDetectResult::kDetectOk;
}

// static
base::Optional<std::tuple<float, float, float>>
FaceDetector::GetCoordinateTransform(const Size src, const Size dst) {
  if (src.width > dst.width || src.height > dst.height) {
    return base::nullopt;
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

int FaceDetector::ScaleImage(buffer_handle_t buffer,
                             Size in_size,
                             Size out_size) {
  struct android_ycbcr ycbcr;
  int ret = buffer_manager_->LockYCbCr(buffer, 0, 0, 0, in_size.width,
                                       in_size.height, &ycbcr);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to map buffer: " << base::safe_strerror(-ret);
    return ret;
  }

  libyuv::ScalePlane(static_cast<uint8_t*>(ycbcr.y), ycbcr.ystride,
                     in_size.width, in_size.height, scaled_buffer_.data(),
                     out_size.width, out_size.width, out_size.height,
                     libyuv::FilterMode::kFilterNone);

  ret = buffer_manager_->Unlock(buffer);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to unmap buffer: " << base::safe_strerror(-ret);
    return ret;
  }
  return 0;
}

}  // namespace cros
