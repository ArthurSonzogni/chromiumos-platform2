/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator.h"

#include <utility>

#include <base/files/file_util.h>

#include "common/still_capture_processor_impl.h"
#include "common/sw_privacy_switch_stream_manipulator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/jpeg_compressor.h"
#include "features/feature_profile.h"
#include "features/zsl/zsl_stream_manipulator.h"
#include "gpu/gpu_resources.h"

#if USE_CAMERA_FEATURE_HDRNET
#include "features/gcam_ae/gcam_ae_stream_manipulator.h"
#include "features/hdrnet/hdrnet_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_AUTO_FRAMING
#include "features/auto_framing/auto_framing_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_EFFECTS
#include "features/effects/effects_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_FACE_DETECTION || USE_CAMERA_FEATURE_AUTO_FRAMING
#include "features/face_detection/face_detection_stream_manipulator.h"
#endif

#if USE_CAMERA_FEATURE_FRAME_ANNOTATOR
#include "features/frame_annotator/frame_annotator_loader_stream_manipulator.h"
#endif

namespace cros {

namespace {

void MaybeEnableHdrNetStreamManipulator(
    const FeatureProfile& feature_profile,
    const StreamManipulator::Options& options,
    GpuResources* gpu_resources,
    std::vector<std::unique_ptr<StreamManipulator>>* out_stream_manipulators) {
#if USE_CAMERA_FEATURE_HDRNET
  if (!feature_profile.IsEnabled(FeatureProfile::FeatureType::kHdrnet)) {
    return;
  }
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
            gpu_resources,
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
#endif
}

void MaybeEnableAutoFramingStreamManipulator(
    const FeatureProfile& feature_profile,
    StreamManipulator::RuntimeOptions* runtime_options,
    GpuResources* gpu_resources,
    std::vector<std::unique_ptr<StreamManipulator>>* out_stream_manipulators) {
#if USE_CAMERA_FEATURE_AUTO_FRAMING
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kAutoFraming)) {
    std::unique_ptr<JpegCompressor> jpeg_compressor =
        JpegCompressor::GetInstance(CameraMojoChannelManager::GetInstance());
    std::unique_ptr<StillCaptureProcessor> still_capture_processor =
        std::make_unique<StillCaptureProcessorImpl>(std::move(jpeg_compressor));
    out_stream_manipulators->emplace_back(
        std::make_unique<AutoFramingStreamManipulator>(
            runtime_options, gpu_resources,
            feature_profile.GetConfigFilePath(
                FeatureProfile::FeatureType::kAutoFraming),
            std::move(still_capture_processor)));
    LOGF(INFO) << "AutoFramingStreamManipulator enabled";
  }
#endif
}

}  // namespace

void StreamManipulator::RuntimeOptions::SetAutoFramingState(
    mojom::CameraAutoFramingState state) {
  base::AutoLock lock(lock_);
  auto_framing_state_ = state;
}

void StreamManipulator::RuntimeOptions::SetSWPrivacySwitchState(
    mojom::CameraPrivacySwitchState state) {
  base::AutoLock lock(lock_);
  sw_privacy_switch_state_ = state;
}

void StreamManipulator::RuntimeOptions::SetEffectsConfig(
    mojom::EffectsConfigPtr config) {
  base::AutoLock lock(lock_);
  effects_config_ = std::move(config);
}

bool StreamManipulator::RuntimeOptions::IsEffectEnabled(
    mojom::CameraEffect effect) {
  base::AutoLock lock(lock_);
  return effects_config_->effect == effect;
}

EffectsConfig StreamManipulator::RuntimeOptions::GetEffectsConfig() {
  base::AutoLock lock(lock_);
  return EffectsConfig{
      .effect = effects_config_->effect,
      .blur_level = effects_config_->blur_level,
      .segmentation_gpu_api = effects_config_->segmentation_gpu_api,
      .graph_max_frames_in_flight = effects_config_->graph_max_frames_in_flight,
  };
}

base::FilePath StreamManipulator::RuntimeOptions::GetDlcRootPath() {
  base::AutoLock lock(lock_);
  return dlc_root_path;
}

void StreamManipulator::RuntimeOptions::SetDlcRootPath(
    const base::FilePath& path) {
  base::AutoLock lock(lock_);
  dlc_root_path = path;
}

mojom::CameraAutoFramingState
StreamManipulator::RuntimeOptions::auto_framing_state() {
  base::AutoLock lock(lock_);
  return auto_framing_state_;
}

mojom::CameraPrivacySwitchState
StreamManipulator::RuntimeOptions::sw_privacy_switch_state() {
  base::AutoLock lock(lock_);
  return sw_privacy_switch_state_;
}

// static
std::vector<std::unique_ptr<StreamManipulator>>
StreamManipulator::GetEnabledStreamManipulators(
    Options options,
    RuntimeOptions* runtime_options,
    GpuResources* gpu_resources,
    CameraMojoChannelManagerToken* mojo_manager_token) {
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  FeatureProfile feature_profile;

#if USE_CAMERA_FEATURE_FRAME_ANNOTATOR
  stream_manipulators.emplace_back(
      std::make_unique<FrameAnnotatorLoaderStreamManipulator>());
  LOGF(INFO) << "FrameAnnotatorLoaderStreamManipulator enabled";
#endif

  MaybeEnableAutoFramingStreamManipulator(feature_profile, runtime_options,
                                          gpu_resources, &stream_manipulators);

#if USE_CAMERA_FEATURE_FACE_DETECTION
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kFaceDetection)) {
    stream_manipulators.emplace_back(
        std::make_unique<FaceDetectionStreamManipulator>(
            feature_profile.GetConfigFilePath(
                FeatureProfile::FeatureType::kFaceDetection)));
    LOGF(INFO) << "FaceDetectionStreamManipulator enabled";
  }
#endif

  MaybeEnableHdrNetStreamManipulator(feature_profile, options, gpu_resources,
                                     &stream_manipulators);

#if USE_CAMERA_FEATURE_EFFECTS
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kEffects)) {
    stream_manipulators.emplace_back(std::make_unique<EffectsStreamManipulator>(
        feature_profile.GetConfigFilePath(
            FeatureProfile::FeatureType::kEffects),
        runtime_options));
    LOGF(INFO) << "EffectsStreamManipulator enabled";
  }
#endif

  // TODO(jcliang): See if we want to move ZSL to feature profile.
  if (options.enable_cros_zsl) {
    stream_manipulators.emplace_back(std::make_unique<ZslStreamManipulator>());
    LOGF(INFO) << "ZslStreamManipulator enabled";
  }

  stream_manipulators.emplace_back(
      std::make_unique<SWPrivacySwitchStreamManipulator>(runtime_options,
                                                         mojo_manager_token));

  return stream_manipulators;
}

}  // namespace cros
