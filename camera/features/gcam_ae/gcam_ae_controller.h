/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_H_
#define CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <base/optional.h>
#include <cutils/native_handle.h>
#include <system/camera_metadata.h>

#include "common/camera_hal3_helpers.h"
#include "features/gcam_ae/ae_info.h"
#include "features/gcam_ae/gcam_ae_device_adapter.h"

namespace cros {

// An interface class to facilitate testing.  For the actual GcamAeController
// implementation, see features/gcam_ae/gcam_ae_controller_impl.{h,cc}.
class GcamAeController {
 public:
  using Factory = base::RepeatingCallback<std::unique_ptr<GcamAeController>(
      const camera_metadata_t* static_info)>;

  struct Options {
    // Whether the GcamAeController is enabled.
    base::Optional<bool> enabled;

    // The duty cycle of the GcamAeController.  The AE controller will
    // calculate and update AE parameters once every |ae_frame_interval| frames.
    base::Optional<int> ae_frame_interval;

    // The maximum allowed HDR ratio.  Needed by Gcam AE as input argument.
    base::Optional<base::flat_map<float, float>> max_hdr_ratio;

    // Whether to use CrOS face detector instead of vendor's implementation for
    // face detection.
    base::Optional<bool> use_cros_face_detector;

    // The duty cycle of the CrOS face detector.  The face detector should run
    // once every |fd_frame_interval| frames.
    base::Optional<int> fd_frame_interval;

    // The AE stats input to Gcam AE.
    base::Optional<AeStatsInputMode> ae_stats_input_mode;

    // The mechanism used to override AE decisions from the camera HAL.
    base::Optional<AeOverrideMode> ae_override_mode;

    // The exposure compensation in stops applied to Gcam AE results.
    base::Optional<float> exposure_compensation;

    // MetadataLogger instance for logging and dumping per-frame metadata.
    // Mainly used for testing and debugging.
    base::Optional<MetadataLogger*> metadata_logger;
  };

  virtual ~GcamAeController() = default;

  // Records the YUV frame of |frame_number| provided in |buffer|.
  // |acquire_fence| is the fence that, if valid, needs to be synced on before
  // accessing |buffer|.  The YUV buffer is normally used for face detection
  // and/or compute the AE stats input to Gcam AE.
  virtual void RecordYuvBuffer(int frame_number,
                               buffer_handle_t buffer,
                               base::ScopedFD acquire_fence) = 0;

  // Records the AE metadata from capture result |result|.  The implementation
  // should use this method to capture the metadata needed for their AE
  // algorithm.
  virtual void RecordAeMetadata(Camera3CaptureDescriptor* result) = 0;

  virtual void SetOptions(const Options& options) = 0;

  // Gets the HDR ratio calculated by Gcam AE.  This is normally used to get the
  // input argument to the HDRnet processing pipeline.
  virtual base::Optional<float> GetCalculatedHdrRatio(
      int frame_number) const = 0;

  // Writes the AE parameters calculated by the AE algorithm in the capture
  // request |request|.
  virtual bool WriteRequestAeParameters(Camera3CaptureDescriptor* request) = 0;

  // Writes the face metadata in the capture result metadata in |result|.
  // This method has effect only when CrOS face detector is enabled, otherwise
  // the face metadata would be filled by the vendor camera HAL.
  virtual bool WriteResultFaceRectangles(Camera3CaptureDescriptor* result) = 0;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_CONTROLLER_H_
