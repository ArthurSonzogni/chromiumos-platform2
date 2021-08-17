/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_AE_CONTROLLER_IMPL_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_AE_CONTROLLER_IMPL_H_

#include "features/hdrnet/hdrnet_ae_controller.h"

#include <array>
#include <memory>
#include <vector>

#include <base/sequence_checker.h>
#include <cros-camera/camera_face_detection.h>

#include "common/metadata_logger.h"
#include "cros-camera/common_types.h"

namespace cros {

class HdrNetAeControllerImpl : public HdrNetAeController {
 public:
  // The default factory method to get the activated HdrNetAeController
  // instance.
  static std::unique_ptr<HdrNetAeController> CreateInstance(
      const camera_metadata_t* static_info);

  HdrNetAeControllerImpl(
      const camera_metadata_t* static_info,
      std::unique_ptr<HdrNetAeDeviceAdapter> ae_device_adapter);

  // HdrNetAeController implementations.
  ~HdrNetAeControllerImpl() = default;
  void RecordYuvBuffer(int frame_number,
                       buffer_handle_t buffer,
                       base::ScopedFD acquire_fence);
  void RecordAeMetadata(Camera3CaptureDescriptor* result);
  void SetOptions(const Options& options);
  float GetCalculatedHdrRatio(int frame_number) const;
  bool WriteRequestAeParameters(Camera3CaptureDescriptor* request);
  bool WriteResultFaceRectangles(Camera3CaptureDescriptor* result);

 private:
  bool ShouldRunAe(int frame_number) const;
  bool ShouldRunFd(int frame_number) const;
  bool ShouldRecordYuvBuffer(int frame_number) const;
  AeFrameInfo& GetOrCreateAeFrameInfoEntry(int frame_number);
  base::Optional<const AeFrameInfo*> GetAeFrameInfoEntry(
      int frame_number) const;
  void MaybeRunAE(int frame_number);
  bool SetExposureCompensation(Camera3CaptureDescriptor* request);
  bool SetManualSensorControls(Camera3CaptureDescriptor* request);

  // AE loop controls.
  bool enabled_ = true;
  int ae_frame_interval_ = 5;

  // Device static metadata.
  Range<int> sensitivity_range_;
  float max_analog_gain_;
  float ae_compensation_step_;
  Range<int> ae_compensation_range_;
  Size active_array_dimension_;

  // Face detector.
  std::unique_ptr<FaceDetector> face_detector_;
  bool use_cros_face_detector_ = false;
  int fd_frame_interval_ = 10;
  // NormalizedRect is defined in the gcam_ae.h header file provided by
  // cros-camera-libhdr.
  std::vector<NormalizedRect> latest_faces_;

  // Ring buffer for the per-frame AE metadata.
  static constexpr size_t kAeFrameInfoRingBufferSize = 12;
  std::array<AeFrameInfo, kAeFrameInfoRingBufferSize> frame_info_;

  // Device-specific AE adapter that handles AE stats extraction and AE
  // parameters computation.
  std::unique_ptr<HdrNetAeDeviceAdapter> ae_device_adapter_;

  // AE algorithm input parameters.
  float max_hdr_ratio_ = 10.0f;
  float base_exposure_compensation_ = 0.0f;
  AeStatsInputMode ae_stats_input_mode_ = AeStatsInputMode::kFromVendorAeStats;

  // AE algorithm outputs.
  float latest_hdr_ratio_ = 0.1f;
  int latest_ae_compensation_ = 0;
  AeParameters latest_ae_parameters_;
  AeOverrideMode ae_override_mode_;

  // Metadata logger for tests and debugging.
  std::unique_ptr<MetadataLogger> metadata_logger_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_AE_CONTROLLER_IMPL_H_
