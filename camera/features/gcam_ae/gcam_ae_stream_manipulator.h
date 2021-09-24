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

#include "features/gcam_ae/gcam_ae_config.h"
#include "features/gcam_ae/gcam_ae_controller.h"

namespace cros {

class GcamAeStreamManipulator : public StreamManipulator {
 public:
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
  GcamAeConfig config_;
  android::CameraMetadata static_info_;

  GcamAeController::Factory gcam_ae_controller_factory_;
  // Access to |ae_controller_| is serialized by |ae_controller_lock_| since the
  // capture requests and results come from different threads.
  base::Lock ae_controller_lock_;
  std::unique_ptr<GcamAeController> ae_controller_
      GUARDED_BY(ae_controller_lock_);

  const camera3_stream_t* yuv_stream_ = nullptr;

  // Metadata logger for tests and debugging.
  std::unique_ptr<MetadataLogger> metadata_logger_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_STREAM_MANIPULATOR_H_
