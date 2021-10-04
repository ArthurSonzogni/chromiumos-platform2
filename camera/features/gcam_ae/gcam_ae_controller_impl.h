/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_IMPL_H_
#define CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_IMPL_H_

#include "features/gcam_ae/gcam_ae_controller.h"

#include <array>
#include <memory>
#include <vector>

#include <base/sequence_checker.h>

#include "common/metadata_logger.h"
#include "cros-camera/common_types.h"
#include "features/gcam_ae/ae_state_machine.h"

namespace cros {

class GcamAeControllerImpl : public GcamAeController {
 public:
  // The default factory method to get the activated GcamAeController
  // instance.
  static std::unique_ptr<GcamAeController> CreateInstance(
      const camera_metadata_t* static_info);

  GcamAeControllerImpl(const camera_metadata_t* static_info,
                       std::unique_ptr<GcamAeDeviceAdapter> ae_device_adapter);

  // GcamAeController implementations.
  ~GcamAeControllerImpl() = default;
  void RecordYuvBuffer(int frame_number,
                       buffer_handle_t buffer,
                       base::ScopedFD acquire_fence) override;
  void RecordAeMetadata(Camera3CaptureDescriptor* result) override;
  void SetOptions(const Options& options) override;
  base::Optional<float> GetCalculatedHdrRatio(int frame_number) override;
  void SetRequestAeParameters(Camera3CaptureDescriptor* request) override;
  void SetResultAeMetadata(Camera3CaptureDescriptor* result) override;

 private:
  void MaybeRunAE(int frame_number);

  // Records the capture settings requested by the camera client, so that we can
  // restore them in the capture result.
  void RecordClientRequestSettings(const Camera3CaptureDescriptor* request);
  // Restores the settings to what the client originally requested.
  void RestoreClientRequestSettings(Camera3CaptureDescriptor* result);

  void SetExposureCompensation(Camera3CaptureDescriptor* request);
  void SetManualSensorControls(Camera3CaptureDescriptor* request);

  // Internal helper methods.
  bool ShouldRunAe(int frame_number) const;
  bool ShouldRunFd(int frame_number) const;
  bool ShouldRecordYuvBuffer(int frame_number) const;

  AeFrameInfo* CreateAeFrameInfoEntry(int frame_number);
  AeFrameInfo* GetAeFrameInfoEntry(int frame_number);

  // AE loop controls.
  bool enabled_ = true;
  int ae_frame_interval_ = 2;
  Range<float> ae_compensation_step_delta_range_;
  int ae_override_interval_while_converging_ = 10;

  AeStateMachine ae_state_machine_;

  // Device static metadata.
  Range<int> sensitivity_range_;
  float max_analog_gain_;
  float max_total_gain_;
  float ae_compensation_step_;
  Range<float> ae_compensation_range_;
  Size active_array_dimension_;

  // Ring buffer for the per-frame AE metadata.
  static constexpr size_t kAeFrameInfoRingBufferSize = 12;
  std::array<AeFrameInfo, kAeFrameInfoRingBufferSize> frame_info_;

  // Device-specific AE adapter that handles AE stats extraction and AE
  // parameters computation.
  std::unique_ptr<GcamAeDeviceAdapter> ae_device_adapter_;

  // AE algorithm input parameters.
  base::flat_map<float, float> max_hdr_ratio_;
  float base_exposure_compensation_ = 0.0f;
  AeStatsInputMode ae_stats_input_mode_ = AeStatsInputMode::kFromVendorAeStats;
  AeOverrideMode ae_override_mode_;

  // AE algorithm outputs.
  float filtered_ae_compensation_steps_ = 0.0f;

  // Metadata logger for tests and debugging.
  MetadataLogger* metadata_logger_ = nullptr;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_IMPL_H_
