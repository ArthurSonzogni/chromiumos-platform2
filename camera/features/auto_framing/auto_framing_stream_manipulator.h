/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <map>
#include <memory>
#include <vector>

#include "common/camera_buffer_pool.h"
#include "common/metadata_logger.h"
#include "common/reloadable_config_file.h"
#include "cros-camera/camera_thread.h"
#include "cros-camera/common_types.h"
#include "features/auto_framing/auto_framing_client.h"
#include "features/auto_framing/face_tracker.h"
#include "features/auto_framing/frame_cropper.h"

namespace cros {

class AutoFramingStreamManipulator : public StreamManipulator {
 public:
  // The default auto framing config file. The file should contain a JSON map
  // for the Options defined below.
  static constexpr const char kDefaultAutoFramingConfigFile[] =
      "/etc/camera/auto_framing_config.json";
  static constexpr const char kOverrideAutoFramingConfigFile[] =
      "/run/camera/auto_framing_config.json";

  enum class Detector {
    // Face detector. It cannot be paired with MotionModel::kLibAutoFraming.
    kFace = 0,
    // Face-Person-Pose detector. The output ROI contains face and part of body
    // regions.
    kFacePersonPose = 1,
  };

  enum class MotionModel {
    // IIR filtering implemented in FrameCropper.
    kIirFilter = 0,
    // Motion model implemented in libautoframing.
    kLibAutoFraming = 1,
  };

  struct Options {
    // The detection model for detecting regions of interest.
    Detector detector = Detector::kFacePersonPose;

    // The motion model for smoothing framing window moves.
    MotionModel motion_model = MotionModel::kLibAutoFraming;

    // Whether the CrOS Auto Framing is enabled.
    bool enable = true;

    // Whether to enable debug mode. In debug mode the frame is not cropped.
    // Instead the ROIs and active crop area is piggybacked in the
    // FACE_RECTANGLES metadata and we can use Chrome Camera App to visualize
    // the auto-framing transition.
    bool debug = false;
  };

  AutoFramingStreamManipulator();
  ~AutoFramingStreamManipulator() override;

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
  struct CaptureContext;

  bool InitializeOnThread(const camera_metadata_t* static_info,
                          CaptureResultCallback result_callback);
  bool ConfigureStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnThread(Camera3CaptureDescriptor* result);

  bool SetUpPipelineOnThread();
  void UpdateFaceRectangleMetadataOnThread(Camera3CaptureDescriptor* result);
  void HandleFramingErrorOnThread(Camera3CaptureDescriptor* result);
  void ResetOnThread();
  void UpdateOptionsOnThread(const base::Value& json_values);

  void OnOptionsUpdated(const base::Value& json_values);

  CaptureContext* CreateCaptureContext(uint32_t frame_number);
  CaptureContext* GetCaptureContext(uint32_t frame_number) const;

  ReloadableConfigFile config_;

  Options options_;

  // Determined by static camera metadata and fixed after Initialize().
  Size active_array_dimension_;
  Size full_frame_size_;

  // Per-stream-config contexts.
  std::vector<camera3_stream_t*> client_streams_;
  camera3_stream_t full_frame_stream_ = {};
  bool override_crop_window_ = false;
  std::map<uint32_t, std::unique_ptr<CaptureContext>> capture_contexts_;

  AutoFramingClient auto_framing_client_;
  std::unique_ptr<FaceTracker> face_tracker_;
  std::unique_ptr<FrameCropper> frame_cropper_;
  std::unique_ptr<CameraBufferPool> full_frame_buffer_pool_;

  std::vector<Rect<float>> faces_;
  Rect<float> region_of_interest_ = {0.0f, 0.0f, 1.0f, 1.0f};
  Rect<float> active_crop_region_ = {0.0f, 0.0f, 1.0f, 1.0f};

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;

  CameraThread thread_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
