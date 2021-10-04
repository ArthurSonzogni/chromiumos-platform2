/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_STREAM_MANIPULATOR_H_

#include <memory>
#include <vector>

#include "common/metadata_logger.h"
#include "common/reloadable_config_file.h"
#include "common/stream_manipulator.h"
#include "cros-camera/camera_face_detection.h"
#include "cros-camera/common_types.h"

namespace cros {

// A wrapper for the FaceSSD-based CrOS face detector.
class FaceDetectionStreamManipulator : public StreamManipulator {
 public:
  // The default face detection config file. The file should contain a JSON map
  // for the options defined below.
  static constexpr const char kDefaultFaceDetectionConfigFile[] =
      "/etc/camera/face_detection_config.json";
  static constexpr const char kOverrideFaceDetectionConfigFile[] =
      "/run/camera/face_detection_config.json";

  struct Options {
    // Uses CrOS face detector for face detection instead of the vendor one.
    bool enable = true;

    // Controls the duty cycle of CrOS face detector. The face detector will run
    // every |fd_frame_interval| frames.
    int fd_frame_interval = 10;

    // Whether to log per-frame metadata using MetadataLogger.
    bool log_frame_metadata = false;
  };

  FaceDetectionStreamManipulator();

  ~FaceDetectionStreamManipulator() override = default;

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;

 private:
  struct FrameInfo {
    int frame_number = -1;
    uint8_t face_detect_mode;
  };

  void RecordClientRequestSettings(Camera3CaptureDescriptor* request);
  void RestoreClientRequestSettings(Camera3CaptureDescriptor* result);
  void SetFaceDetectionMode(Camera3CaptureDescriptor* request);
  void SetResultAeMetadata(Camera3CaptureDescriptor* result);
  FrameInfo& GetOrCreateFrameInfoEntry(int frame_number);
  void OnOptionsUpdated(const base::Value& json_values);

  // Face detector settings.
  std::unique_ptr<FaceDetector> face_detector_;
  ReloadableConfigFile config_;
  Options options_;
  Size active_array_dimension_;
  uint8_t active_face_detect_mode_ = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;

  // The YUV stream to run the face detector on.
  const camera3_stream_t* yuv_stream_ = nullptr;

  // Protects |latest_faces_| and |frame_info_| since they can be accessed on
  // different threads.
  base::Lock lock_;

  // The latest face ROIs detected by the CrOS face detector.
  std::vector<Rect<float>> latest_faces_ GUARDED_BY(lock_);

  // Ring buffer for the per-frame face detection metadata.
  static constexpr size_t kFrameInfoRingBufferSize = 12;
  std::array<FrameInfo, kFrameInfoRingBufferSize> frame_info_ GUARDED_BY(lock_);

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_FACE_DETECTION_FACE_DETECTION_STREAM_MANIPULATOR_H_
