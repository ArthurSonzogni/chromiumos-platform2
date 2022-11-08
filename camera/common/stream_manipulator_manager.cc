/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>

#include "common/camera_hal3_helpers.h"
#include "common/common_tracing.h"
#include "common/still_capture_processor_impl.h"
#include "common/stream_manipulator.h"
#include "common/sw_privacy_switch_stream_manipulator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "cros-camera/jpeg_compressor.h"
#include "cros-camera/tracing.h"
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

StreamManipulatorManager::StreamManipulatorManager(
    StreamManipulator::Options options,
    StreamManipulator::RuntimeOptions* runtime_options,
    GpuResources* gpu_resources,
    CameraMojoChannelManagerToken* mojo_manager_token) {
  TRACE_COMMON();
  FeatureProfile feature_profile;

#if USE_CAMERA_FEATURE_FRAME_ANNOTATOR
  stream_manipulators_.emplace_back(
      std::make_unique<FrameAnnotatorLoaderStreamManipulator>());
  LOGF(INFO) << "FrameAnnotatorLoaderStreamManipulator enabled";
#endif

  MaybeEnableAutoFramingStreamManipulator(feature_profile, runtime_options,
                                          gpu_resources, &stream_manipulators_);

#if USE_CAMERA_FEATURE_FACE_DETECTION
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kFaceDetection)) {
    stream_manipulators_.emplace_back(
        std::make_unique<FaceDetectionStreamManipulator>(
            feature_profile.GetConfigFilePath(
                FeatureProfile::FeatureType::kFaceDetection)));
    LOGF(INFO) << "FaceDetectionStreamManipulator enabled";
  }
#endif

  MaybeEnableHdrNetStreamManipulator(feature_profile, options, gpu_resources,
                                     &stream_manipulators_);

#if USE_CAMERA_FEATURE_EFFECTS
  if (feature_profile.IsEnabled(FeatureProfile::FeatureType::kEffects)) {
    stream_manipulators_.emplace_back(
        std::make_unique<EffectsStreamManipulator>(
            feature_profile.GetConfigFilePath(
                FeatureProfile::FeatureType::kEffects),
            runtime_options));
    LOGF(INFO) << "EffectsStreamManipulator enabled";
  }
#endif

  // TODO(jcliang): See if we want to move ZSL to feature profile.
  stream_manipulators_.emplace_back(std::make_unique<ZslStreamManipulator>());
  LOGF(INFO) << "ZslStreamManipulator enabled";

  stream_manipulators_.emplace_back(
      std::make_unique<SWPrivacySwitchStreamManipulator>(runtime_options,
                                                         mojo_manager_token));
}

bool StreamManipulatorManager::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::CaptureResultCallback result_callback) {
  TRACE_COMMON();
  for (size_t i = 0; i < stream_manipulators_.size(); ++i) {
    stream_manipulators_[i]->Initialize(
        static_info,
        base::BindRepeating(
            [](StreamManipulator::CaptureResultCallback result_callback,
               base::span<std::unique_ptr<StreamManipulator>> upper_sms,
               Camera3CaptureDescriptor result) {
              for (auto it = upper_sms.rbegin(); it != upper_sms.rend(); ++it) {
                (*it)->ProcessCaptureResult(&result);
              }
              result_callback.Run(std::move(result));
            },
            result_callback, base::make_span(stream_manipulators_.begin(), i)));
  }
  return true;
}

bool StreamManipulatorManager::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_COMMON();
  for (auto& stream_manipulator : stream_manipulators_) {
    stream_manipulator->ConfigureStreams(stream_config);
  }
  return true;
}

bool StreamManipulatorManager::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_COMMON();
  // Call OnConfiguredStreams in reverse order so the stream manipulators can
  // unwind the stream modifications.
  for (auto it = stream_manipulators_.rbegin();
       it != stream_manipulators_.rend(); ++it) {
    (*it)->OnConfiguredStreams(stream_config);
  }
  return true;
}

bool StreamManipulatorManager::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  TRACE_COMMON();
  for (auto& stream_manipulator : stream_manipulators_) {
    stream_manipulator->ConstructDefaultRequestSettings(
        default_request_settings, type);
  }
  return true;
}

bool StreamManipulatorManager::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  TRACE_COMMON();
  for (size_t i = 0; i < stream_manipulators_.size(); ++i) {
    if (camera_metadata_inspector_ &&
        camera_metadata_inspector_->IsPositionInspected(i)) {
      camera_metadata_inspector_->InspectRequest(request->LockForRequest(), i);
      request->Unlock();
    }
    {
      TRACE_EVENT(kCameraTraceCategoryCommon, "SM::ProcessCaptureRequest",
                  kCameraTraceKeyFrameNumber, request->frame_number());
      stream_manipulators_[i]->ProcessCaptureRequest(request);
    }
  }
  if (camera_metadata_inspector_ &&
      camera_metadata_inspector_->IsPositionInspected(
          stream_manipulators_.size())) {
    camera_metadata_inspector_->InspectRequest(request->LockForRequest(),
                                               stream_manipulators_.size());
    request->Unlock();
  }
  return true;
}

bool StreamManipulatorManager::Flush() {
  TRACE_COMMON();
  for (auto& stream_manipulator : stream_manipulators_) {
    stream_manipulator->Flush();
  }
  return true;
}

bool StreamManipulatorManager::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  TRACE_COMMON();
  // Call ProcessCaptureResult in reverse order of that of ProcessCaptureRequest
  // so the stream manipulators can unwind the buffer states.
  if (camera_metadata_inspector_ &&
      camera_metadata_inspector_->IsPositionInspected(
          stream_manipulators_.size())) {
    camera_metadata_inspector_->InspectResult(result->LockForResult(),
                                              stream_manipulators_.size());
    result->Unlock();
  }
  for (size_t i = 0; i < stream_manipulators_.size(); ++i) {
    size_t j = stream_manipulators_.size() - i - 1;
    {
      TRACE_EVENT(kCameraTraceCategoryCommon, "SM::ProcessCaptureResult",
                  kCameraTraceKeyFrameNumber, result->frame_number());
      stream_manipulators_[j]->ProcessCaptureResult(result);
    }
    if (camera_metadata_inspector_ &&
        camera_metadata_inspector_->IsPositionInspected(j)) {
      camera_metadata_inspector_->InspectResult(result->LockForResult(), j);
      result->Unlock();
    }
  }
  return true;
}

bool StreamManipulatorManager::Notify(camera3_notify_msg_t* msg) {
  TRACE_COMMON();
  for (auto it = stream_manipulators_.rbegin();
       it < stream_manipulators_.rend(); ++it) {
    (*it)->Notify(msg);
  }
  return true;
}

}  // namespace cros
