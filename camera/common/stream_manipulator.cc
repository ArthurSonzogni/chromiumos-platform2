/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator.h"

#include <utility>

#include <base/files/file_util.h>

#include "cros-camera/constants.h"
#include "features/feature_profile.h"
#include "features/zsl/zsl_stream_manipulator.h"

#if USE_CAMERA_FEATURE_HDRNET
#include "common/still_capture_processor_impl.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/jpeg_compressor.h"
#include "features/gcam_ae/gcam_ae_stream_manipulator.h"
#include "features/hdrnet/hdrnet_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_AUTO_FRAMING
#include "features/auto_framing/auto_framing_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_FACE_DETECTION || USE_CAMERA_FEATURE_AUTO_FRAMING
#include "features/face_detection/face_detection_stream_manipulator.h"
#endif

namespace cros {

namespace {

void MaybeEnableHdrNetStreamManipulator(
    const StreamManipulator::Options& options,
    std::vector<std::unique_ptr<StreamManipulator>>* out_stream_manipulators) {
#if USE_CAMERA_FEATURE_HDRNET
  FeatureProfile feature_profile;

  if (base::PathExists(base::FilePath(constants::kForceDisableHdrNetPath))) {
    // HDRnet is forcibly disabled.
    return;
  }

  if (base::PathExists(base::FilePath(constants::kForceEnableHdrNetPath)) ||
      feature_profile.IsEnabled(FeatureProfile::FeatureType::kHdrnet)) {
    // HDRnet is enabled forcibly or by the device setting.

    constexpr const char kIntelIpu6CameraModuleName[] =
        "Intel IPU6 Camera HAL Module";
    if (options.camera_module_name == kIntelIpu6CameraModuleName) {
      // The pipeline looks like:
      //        ____       ________       _________
      //   --> |    | --> |        | --> |         | -->
      //       | FD |     | HDRnet |     | Gcam AE |
      //   <== |____| <== |________| <== |_________| <==
      //
      //   --> capture request flow
      //   ==> capture result flow
      //
      // Why the pipeline is organized this way:
      // * FaceDetection (if present) is placed before HDRnet because we want to
      //   run face detection on result frames rendered by HDRnet so we can
      //   better detect the underexposed faces.
      // * Gcam AE is placed after HDRnet because it needs raw result frames as
      //   input to get accurate AE metering, and because Gcam AE produces the
      //   HDR ratio needed by HDRnet to render the output frame.
      std::unique_ptr<JpegCompressor> jpeg_compressor =
          JpegCompressor::GetInstance(CameraMojoChannelManager::GetInstance());
      out_stream_manipulators->emplace_back(
          std::make_unique<HdrNetStreamManipulator>(
              feature_profile.GetConfigFilePath(
                  FeatureProfile::FeatureType::kHdrnet),
              std::make_unique<StillCaptureProcessorImpl>(
                  std::move(jpeg_compressor))));
      LOGF(INFO) << "HdrNetStreamManipulator enabled";
      if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kGcamAe)) {
        out_stream_manipulators->emplace_back(
            std::make_unique<GcamAeStreamManipulator>(
                feature_profile.GetConfigFilePath(
                    FeatureProfile::FeatureType::kGcamAe)));
        LOGF(INFO) << "GcamAeStreamManipulator enabled";
      }
    }
  }
#endif
}

void MaybeEnableAutoFramingStreamManipulator(
    std::vector<std::unique_ptr<StreamManipulator>>* out_stream_manipulators) {
#if USE_CAMERA_FEATURE_AUTO_FRAMING
  if (base::PathExists(
          base::FilePath(constants::kForceDisableAutoFramingPath))) {
    // Auto-framing is forcibly disabled.
    return;
  }
  if (base::PathExists(
          base::FilePath(constants::kForceEnableAutoFramingPath))) {
    // Auto-framing is forcibly enabled.
    out_stream_manipulators->emplace_back(
        std::make_unique<AutoFramingStreamManipulator>());
    LOGF(INFO) << "AutoFramingStreamManipulator enabled";
  }
#endif
}

}  // namespace

// static
std::vector<std::unique_ptr<StreamManipulator>>
StreamManipulator::GetEnabledStreamManipulators(Options options) {
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  FeatureProfile feature_profile;

  MaybeEnableAutoFramingStreamManipulator(&stream_manipulators);

#if USE_CAMERA_FEATURE_FACE_DETECTION
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kFaceDetection)) {
    stream_manipulators.emplace_back(
        std::make_unique<FaceDetectionStreamManipulator>(
            feature_profile.GetConfigFilePath(
                FeatureProfile::FeatureType::kFaceDetection)));
    LOGF(INFO) << "FaceDetectionStreamManipulator enabled";
  }
#endif

  MaybeEnableHdrNetStreamManipulator(options, &stream_manipulators);

  // TODO(jcliang): See if we want to move ZSL to feature profile.
  if (options.enable_cros_zsl) {
    stream_manipulators.emplace_back(std::make_unique<ZslStreamManipulator>());
    LOGF(INFO) << "ZslStreamManipulator enabled";
  }

  return stream_manipulators;
}

}  // namespace cros
