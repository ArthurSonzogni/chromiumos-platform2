/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_GCAM_AE_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_GCAM_AE_GCAM_AE_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <memory>

#include <base/callback_helpers.h>
#include <base/synchronization/lock.h>
#include <camera/camera_metadata.h>

#include "common/reloadable_config_file.h"
#include "features/gcam_ae/gcam_ae_controller.h"

namespace cros {

class GcamAeStreamManipulator : public StreamManipulator {
 public:
  // The default Gcam AE config file. The file should contain a JSON map for the
  // options defined below.
  static constexpr const char kDefaultGcamAeConfigFile[] =
      "/etc/camera/gcam_ae_config.json";
  static constexpr const char kOverrideGcamAeConfigFile[] =
      "/run/camera/gcam_ae_config.json";

  struct Options {
    // Enables Gcam AE to produce exposure settings and HDR ratio.
    bool gcam_ae_enable = true;

    // The duty cycle of the GcamAeAeController. The AE controller will
    // calculate and update AE parameters once every |ae_frame_interval| frames.
    int ae_frame_interval = 2;

    // A map with (gain, max_hdr_ratio) entries defining the max HDR ratio
    // passed to Gcam AE based on the gain (analog * digital) used to capture
    // the frame.
    base::flat_map<float, float> max_hdr_ratio = {{1.0, 5.0},  {2.0, 5.0},
                                                  {4.0, 5.0},  {8.0, 4.0},
                                                  {16.0, 2.0}, {32.0, 1.1}};

    // Controls how Gcam AE gets the AE stats input parameters.
    AeStatsInputMode ae_stats_input_mode = AeStatsInputMode::kFromVendorAeStats;

    // Controls how GcamAeController overrides camera HAL's AE decision.
    AeOverrideMode ae_override_mode = AeOverrideMode::kWithManualSensorControl;

    // Uses CrOS face detector for face detection instead of the vendor one.
    bool use_cros_face_detector = true;

    // Controls the duty cycle of CrOS face detector. The face detector will run
    // every |fd_frame_interval| frames.
    int fd_frame_interval = 10;

    // The exposure compensation in stops set to every capture request.
    float exposure_compensation = 0.0f;

    // Whether to log per-frame metadata using MetadataLogger.
    bool log_frame_metadata = false;
  };

  GcamAeStreamManipulator(GcamAeController::Factory gcam_ae_controller_factory =
                              base::NullCallback());

  ~GcamAeStreamManipulator() override = default;

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

  ReloadableConfigFile config_;
  Options options_;
  android::CameraMetadata static_info_;

  GcamAeController::Factory gcam_ae_controller_factory_;
  // Access to |ae_controller_| is serialized by |ae_controller_lock_| since the
  // capture requests and results come from different threads.
  base::Lock ae_controller_lock_;
  std::unique_ptr<GcamAeController> ae_controller_
      GUARDED_BY(ae_controller_lock_);

  const camera3_stream_t* yuv_stream_ = nullptr;

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_STREAM_MANIPULATOR_H_
