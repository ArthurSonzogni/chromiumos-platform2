/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <memory>
#include <vector>

#include "common/metadata_logger.h"
#include "common/reloadable_config_file.h"
#include "cros-camera/camera_thread.h"
#include "cros-camera/common_types.h"
#include "features/auto_framing/face_tracker.h"
#include "features/auto_framing/frame_cropper.h"

namespace cros {

// For GPU-based implementation:
// - Get the face ROIs from the feature metadata in request
// - Temporal-filter and compute the
class AutoFramingStreamManipulator : public StreamManipulator {
 public:
  // The default auto framing config file. The file should contain a JSON map
  // for the options defined below.
  static constexpr const char kDefaultAutoFramingConfigFile[] =
      "/etc/camera/auto_framing_config.json";
  static constexpr const char kOverrideAutoFramingConfigFile[] =
      "/run/camera/auto_framing_config.json";

  struct Options {
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
  void OnOptionsUpdated(const base::Value& json_values);
  void Reset();

  void CropBuffer(Camera3CaptureDescriptor* result);
  void UpdateFaceRectangleMetadata(Camera3CaptureDescriptor* result);

  ReloadableConfigFile config_;
  Options options_;

  // Static camera metadata needed for auto framing to work.
  Size active_array_dimension_;

  CameraThread gpu_thread_;

  base::Lock lock_;
  std::unique_ptr<FaceTracker> face_tracker_ GUARDED_BY(lock_);
  std::unique_ptr<FrameCropper> frame_cropper_ GUARDED_BY(lock_);

  // The YUV stream to run auto framing on.
  const camera3_stream_t* yuv_stream_ GUARDED_BY(lock_) = nullptr;

  std::vector<Rect<float>> faces_;
  Rect<float> region_of_interest_ = {0.0f, 0.0f, 1.0f, 1.0f};
  Rect<float> active_crop_region_ = {0.0f, 0.0f, 1.0f, 1.0f};

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
