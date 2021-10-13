/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/face_detection/face_detection_stream_manipulator.h"

#include <utility>

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] =
    "/run/camera/face_detection_frame_metadata.json";

constexpr char kFaceDetectionEnableKey[] = "face_detection_enable";
constexpr char kFdFrameIntervalKey[] = "fd_frame_interval";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";

constexpr char kTagFaceRectangles[] = "face_rectangles";

}  // namespace

//
// FaceDetectionStreamManipulator implementations.
//

FaceDetectionStreamManipulator::FaceDetectionStreamManipulator()
    : face_detector_(FaceDetector::Create()),
      config_(kDefaultFaceDetectionConfigFile,
              kOverrideFaceDetectionConfigFile),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {
  config_.SetCallback(
      base::BindRepeating(&FaceDetectionStreamManipulator::OnOptionsUpdated,
                          base::Unretained(this)));
}

bool FaceDetectionStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  VLOGF(2) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
  return true;
}

bool FaceDetectionStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  yuv_stream_ = nullptr;

  for (auto* s : stream_config->GetStreams()) {
    if (s->stream_type != CAMERA3_STREAM_OUTPUT) {
      continue;
    }

    // TODO(jcliang): See if we need to support 10-bit YUV (i.e. with format
    // HAL_PIXEL_FORMAT_YCBCR_P010);
    if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        continue;
      }

      // Pick the buffer with the largest width to AE controller. This is a
      // heuristic and shouldn't matter for the majority of the time, as for
      // most cases the requested streams would have the same aspect ratio.
      if (!yuv_stream_ || s->width > yuv_stream_->width) {
        yuv_stream_ = s;
      }
    }
  }

  if (yuv_stream_) {
    VLOGF(1) << "YUV stream for CrOS Face Detection processing: "
             << GetDebugString(yuv_stream_);
  } else {
    LOGF(WARNING)
        << "No YUV stream suitable for CrOS Face Detection processing";
  }

  return true;
}

bool FaceDetectionStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool FaceDetectionStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool FaceDetectionStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (!options_.enable) {
    return true;
  }
  if (request->GetInputBuffer() != nullptr) {
    // Skip reprocessing requests.
    return true;
  }

  base::AutoLock lock(lock_);

  // Disable face detection in the vendor camera HAL in favor of our CrOS face
  // detector.
  RecordClientRequestSettings(request);

  // Only change the metadata when the client request settings is not null.
  // This is mainly to make the CTS tests happy, as some test cases set null
  // settings and if we change that the vendor camera HAL may not handle the
  // incremental changes well.
  if (request->has_metadata()) {
    SetFaceDetectionMode(request);
  }

  // Carry down the latest detected faces as Gcam AE's input metadata.
  request->feature_metadata().faces = latest_faces_;
  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number()) << "Set face rectangles:";
    for (const auto& f : *request->feature_metadata().faces) {
      VLOGFID(2, request->frame_number())
          << "(" << f.left << ", " << f.top << ", " << f.right() << ", "
          << f.bottom() << ")";
    }
  }

  return true;
}

bool FaceDetectionStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  if (!options_.enable) {
    return true;
  }

  base::AutoLock lock(lock_);

  if (result->frame_number() % options_.fd_frame_interval == 0 &&
      result->num_output_buffers() > 0) {
    for (auto& buffer : result->GetOutputBuffers()) {
      if (buffer.stream == yuv_stream_) {
        std::vector<human_sensing::CrosFace> facessd_faces;
        auto ret = face_detector_->Detect(*buffer.buffer, &facessd_faces,
                                          active_array_dimension_);
        std::vector<Rect<float>> faces;
        if (ret != FaceDetectResult::kDetectOk) {
          LOGF(WARNING) << "Cannot run face detection";
        } else {
          for (auto& f : facessd_faces) {
            faces.push_back(Rect<float>(
                /*left=*/std::clamp(
                    f.bounding_box.x1 / active_array_dimension_.width, 0.0f,
                    1.0f),
                /*top=*/
                std::clamp(f.bounding_box.y1 / active_array_dimension_.height,
                           0.0f, 1.0f),
                /*width=*/
                std::clamp((f.bounding_box.x2 - f.bounding_box.x1) /
                               active_array_dimension_.width,
                           0.0f, 1.0f),
                /*height=*/
                std::clamp((f.bounding_box.y2 - f.bounding_box.y1) /
                               active_array_dimension_.height,
                           0.0f, 1.0f)));
          }
          if (VLOG_IS_ON(2)) {
            VLOGFID(2, result->frame_number()) << "Detected face:";
            for (const auto& f : faces) {
              VLOGFID(2, result->frame_number())
                  << "(" << f.left << ", " << f.top << ", " << f.right() << ", "
                  << f.bottom() << ")";
            }
          }
        }
        latest_faces_ = std::move(faces);
        break;
      }
    }
  }

  if (options_.log_frame_metadata) {
    std::vector<float> flattened_faces(latest_faces_.size() * 4);
    for (int i = 0; i < latest_faces_.size(); ++i) {
      const Rect<float>& f = latest_faces_[i];
      const int base = i * 4;
      flattened_faces[base] = f.left;
      flattened_faces[base + 1] = f.top;
      flattened_faces[base + 2] = f.right();
      flattened_faces[base + 3] = f.bottom();
    }
    metadata_logger_.Log(result->frame_number(), kTagFaceRectangles,
                         base::span<const float>(flattened_faces.data(),
                                                 flattened_faces.size()));
  }

  // Report the face rectangles in result metadata. Restore the metadata to
  // what originally requested by the client so the metadata overridden by us is
  // transparent to the client.
  SetResultAeMetadata(result);
  RestoreClientRequestSettings(result);

  return true;
}

bool FaceDetectionStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool FaceDetectionStreamManipulator::Flush() {
  return true;
}

void FaceDetectionStreamManipulator::RecordClientRequestSettings(
    Camera3CaptureDescriptor* request) {
  FrameInfo& frame_info = GetOrCreateFrameInfoEntry(request->frame_number());
  base::span<const uint8_t> face_detect_mode =
      request->GetMetadata<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE);
  if (!face_detect_mode.empty()) {
    VLOGFID(2, request->frame_number())
        << "Client requested ANDROID_STATISTICS_FACE_DETECT_MODE="
        << static_cast<int>(face_detect_mode[0]);
    active_face_detect_mode_ = face_detect_mode[0];
  }
  frame_info.face_detect_mode = active_face_detect_mode_;
}

void FaceDetectionStreamManipulator::RestoreClientRequestSettings(
    Camera3CaptureDescriptor* result) {
  if (!result->has_metadata()) {
    return;
  }
  FrameInfo& frame_info = GetOrCreateFrameInfoEntry(result->frame_number());
  std::array<uint8_t, 1> face_detect_mode = {frame_info.face_detect_mode};
  if (!result->UpdateMetadata<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE,
                                       face_detect_mode)) {
    LOGF(ERROR) << "Cannot restore ANDROID_STATISTICS_FACE_DETECT_MODE";
  } else {
    VLOGFID(2, result->frame_number())
        << "Restored ANDROID_STATISTICS_FACE_DETECT_MODE="
        << static_cast<int>(frame_info.face_detect_mode);
  }
}

void FaceDetectionStreamManipulator::SetFaceDetectionMode(
    Camera3CaptureDescriptor* request) {
  FrameInfo& frame_info = GetOrCreateFrameInfoEntry(request->frame_number());

  if (frame_info.face_detect_mode != ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
    // Turn off the vendor camera HAL's face detection in favor of CrOS face
    // detector.
    std::array<uint8_t, 1> face_detect_mode = {
        ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    if (!request->UpdateMetadata<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE,
                                          face_detect_mode)) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_DETECT_MODE to OFF";
    } else {
      VLOGFID(2, request->frame_number())
          << "Set ANDROID_STATISTICS_FACE_DETECT_MODE to OFF";
    }
  }
}

void FaceDetectionStreamManipulator::SetResultAeMetadata(
    Camera3CaptureDescriptor* result) {
  if (!result->has_metadata()) {
    return;
  }

  FrameInfo& frame_info = GetOrCreateFrameInfoEntry(result->frame_number());
  if (frame_info.face_detect_mode != ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
    // This is mainly for displaying the face rectangles in camera app for
    // development and debugging.
    std::vector<int32_t> face_coordinates;
    for (const auto& f : latest_faces_) {
      face_coordinates.push_back(f.left * active_array_dimension_.width);
      face_coordinates.push_back(f.top * active_array_dimension_.height);
      face_coordinates.push_back(f.right() * active_array_dimension_.width);
      face_coordinates.push_back(f.bottom() * active_array_dimension_.height);
    }
    if (!result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                         face_coordinates)) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
    }
  }
}

FaceDetectionStreamManipulator::FrameInfo&
FaceDetectionStreamManipulator::GetOrCreateFrameInfoEntry(int frame_number) {
  FrameInfo& frame_info = frame_info_[frame_number % kFrameInfoRingBufferSize];
  if (frame_info.frame_number != frame_number) {
    frame_info = FrameInfo{.frame_number = frame_number};
  }
  return frame_info;
}

void FaceDetectionStreamManipulator::OnOptionsUpdated(
    const base::Value& json_values) {
  auto fd_enable = json_values.FindBoolKey(kFaceDetectionEnableKey);
  if (fd_enable) {
    options_.enable = *fd_enable;
  }
  auto fd_frame_interval = json_values.FindIntKey(kFdFrameIntervalKey);
  if (fd_frame_interval) {
    options_.fd_frame_interval = *fd_frame_interval;
  }
  auto log_frame_metadata = json_values.FindBoolKey(kLogFrameMetadataKey);
  if (log_frame_metadata) {
    if (options_.log_frame_metadata && !log_frame_metadata.value()) {
      // Dump frame metadata when metadata logging is turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = *log_frame_metadata;
  }

  VLOGF(1) << "Face detection config:"
           << " use_cros_face_detector=" << options_.enable
           << " fd_frame_interval=" << options_.fd_frame_interval;
}

}  // namespace cros
