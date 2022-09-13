/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/face_detection/face_detection_stream_manipulator.h"

#include <algorithm>
#include <utility>

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] =
    "/run/camera/face_detection_frame_metadata.json";

constexpr char kFaceDetectionEnableKey[] = "face_detection_enable";
constexpr char kFdFrameIntervalKey[] = "fd_frame_interval";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";
constexpr char kDebugKey[] = "debug";

constexpr char kTagFaceRectangles[] = "face_rectangles";

void LogFaceInfo(int frame_number, const human_sensing::CrosFace& face) {
  VLOGFID(2, frame_number) << "\t(" << face.bounding_box.x1 << ", "
                           << face.bounding_box.y1 << ", "
                           << face.bounding_box.x2 << ", "
                           << face.bounding_box.y2 << ")";
  VLOGFID(2, frame_number) << "\tLandmarks:";
  for (const auto& l : face.landmarks) {
    VLOGFID(2, frame_number) << "\t(" << l.x << ", " << l.y << ", " << l.z
                             << "): " << LandmarkTypeToString(l.type);
  }
  VLOGFID(2, frame_number) << "Roll angle: " << face.roll_angle;
  VLOGFID(2, frame_number) << "Pan angle: " << face.pan_angle;
  VLOGFID(2, frame_number) << "Tilt angle: " << face.tilt_angle;
}

}  // namespace

//
// FaceDetectionStreamManipulator implementations.
//

FaceDetectionStreamManipulator::FaceDetectionStreamManipulator(
    base::FilePath config_file_path)
    : face_detector_(FaceDetector::Create()),
      config_(ReloadableConfigFile::Options{
          config_file_path, base::FilePath(kOverrideFaceDetectionConfigFile)}),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {
  if (!config_.IsValid()) {
    LOGF(ERROR) << "Cannot load valid config; turn off feature by default";
    options_.enable = false;
  }
  config_.SetCallback(
      base::BindRepeating(&FaceDetectionStreamManipulator::OnOptionsUpdated,
                          base::Unretained(this)));
}

bool FaceDetectionStreamManipulator::Initialize(
    GpuResources* gpu_resources_,
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
  auto is_larger_or_closer_to_native_aspect_ratio =
      [&](const camera3_stream_t* lhs, const camera3_stream_t* rhs) -> bool {
    if (lhs->width >= rhs->width && lhs->height >= rhs->height) {
      return true;
    }
    if (lhs->width <= rhs->width && lhs->height <= rhs->height) {
      return false;
    }
    float active_aspect_ratio =
        static_cast<float>(active_array_dimension_.width) /
        static_cast<float>(active_array_dimension_.height);
    float lhs_aspect_ratio =
        static_cast<float>(lhs->width) / static_cast<float>(lhs->height);
    float rhs_aspect_ratio =
        static_cast<float>(rhs->width) / static_cast<float>(rhs->height);
    return std::abs(lhs_aspect_ratio - active_aspect_ratio) <=
           std::abs(rhs_aspect_ratio - active_aspect_ratio);
  };

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

      // Pick a buffer for AE controller. This is a heuristic and shouldn't
      // matter for the majority of the time, as for most cases the requested
      // streams would have the same aspect ratio.
      if (!yuv_stream_ ||
          is_larger_or_closer_to_native_aspect_ratio(s, yuv_stream_)) {
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
    VLOGFID(2, request->frame_number()) << "Set face(s):";
    for (const auto& f : *request->feature_metadata().faces) {
      LogFaceInfo(request->frame_number(), f);
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
        face_detector_->DetectAsync(
            *buffer.buffer, active_array_dimension_,
            base::BindOnce(&FaceDetectionStreamManipulator::OnFaceDetected,
                           base::Unretained(this), result->frame_number()));
        break;
      }
    }
  }

  if (options_.log_frame_metadata) {
    std::vector<float> flattened_faces(latest_faces_.size() * 4);
    // Log the face rectangles in normalized rectangles so that it can be
    // consumed by the Gcam AE CLI directly.
    for (int i = 0; i < latest_faces_.size(); ++i) {
      const human_sensing::CrosFace& f = latest_faces_[i];
      const int base = i * 4;
      // Left
      flattened_faces[base] = std::clamp(
          f.bounding_box.x1 / active_array_dimension_.width, 0.0f, 1.0f);
      // Top
      flattened_faces[base + 1] = std::clamp(
          f.bounding_box.y1 / active_array_dimension_.height, 0.0f, 1.0f);
      // Right
      flattened_faces[base + 2] = std::clamp(
          f.bounding_box.x2 / active_array_dimension_.width, 0.0f, 1.0f);
      // Bottom
      flattened_faces[base + 3] = std::clamp(
          f.bounding_box.y2 / active_array_dimension_.height, 0.0f, 1.0f);
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
  if (options_.debug &&
      face_detect_mode[0] == ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
    face_detect_mode[0] = ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE;
  }
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
  if (options_.debug ||
      frame_info.face_detect_mode != ANDROID_STATISTICS_FACE_DETECT_MODE_OFF) {
    std::vector<int32_t> face_coordinates;
    std::vector<uint8_t> face_scores;
    for (const auto& f : latest_faces_) {
      face_coordinates.push_back(f.bounding_box.x1);
      face_coordinates.push_back(f.bounding_box.y1);
      face_coordinates.push_back(f.bounding_box.x2);
      face_coordinates.push_back(f.bounding_box.y2);
      face_scores.push_back(static_cast<uint8_t>(
          std::clamp(f.confidence * 100.0f, 0.0f, 100.0f)));
    }
    if (!result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                         face_coordinates)) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
    }
    if (!result->UpdateMetadata<uint8_t>(ANDROID_STATISTICS_FACE_SCORES,
                                         face_scores)) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_SCORES";
    }
    if (frame_info.face_detect_mode ==
        ANDROID_STATISTICS_FACE_DETECT_MODE_FULL) {
      NOTIMPLEMENTED() << "FULL mode requires FACE_IDS and FACE_LANDMARKS";
    }
  }

  result->feature_metadata().faces = latest_faces_;
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
  LoadIfExist(json_values, kFaceDetectionEnableKey, &options_.enable);
  LoadIfExist(json_values, kFdFrameIntervalKey, &options_.fd_frame_interval);
  LoadIfExist(json_values, kDebugKey, &options_.debug);

  bool log_frame_metadata;
  if (LoadIfExist(json_values, kLogFrameMetadataKey, &log_frame_metadata)) {
    if (options_.log_frame_metadata && !log_frame_metadata) {
      // Dump frame metadata when metadata logging is turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = log_frame_metadata;
  }

  VLOGF(1) << "Face detection config:"
           << " use_cros_face_detector=" << options_.enable
           << " fd_frame_interval=" << options_.fd_frame_interval;
}

void FaceDetectionStreamManipulator::OnFaceDetected(
    uint32_t frame_number,
    FaceDetectResult detect_result,
    std::vector<human_sensing::CrosFace> faces) {
  if (detect_result != FaceDetectResult::kDetectOk) {
    LOGF(WARNING) << "Failed to run face detection";
    return;
  }
  if (VLOG_IS_ON(2)) {
    if (faces.empty()) {
      VLOGFID(2, frame_number) << "Detected zero faces";
    } else {
      VLOGFID(2, frame_number) << "Detected " << faces.size() << " face(s):";
      for (const auto& f : faces) {
        LogFaceInfo(frame_number, f);
      }
    }
  }
  base::AutoLock lock(lock_);
  latest_faces_ = std::move(faces);
}

}  // namespace cros
