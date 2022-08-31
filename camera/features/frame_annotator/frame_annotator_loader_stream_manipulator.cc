/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/frame_annotator/frame_annotator_loader_stream_manipulator.h"

#include <dlfcn.h>

#include "features/frame_annotator/libs/utils.h"

namespace cros {

namespace {

constexpr char kFrameAnnotatorLibPath[] =
    "/usr/lib64/libcros_camera_frame_annotator.so";

}

//
// FrameAnnotatorLoaderStreamManipulator implementations.
//

FrameAnnotatorLoaderStreamManipulator::FrameAnnotatorLoaderStreamManipulator()
    : frame_annotator_lib_(base::FilePath(kFrameAnnotatorLibPath)) {
  if (auto make_frame_annotator_stream_manipulator =
          reinterpret_cast<decltype(&MakeFrameAnnotatorStreamManipulator)>(
              frame_annotator_lib_.GetFunctionPointer(
                  "MakeFrameAnnotatorStreamManipulator"))) {
    stream_manipulator_ = std::unique_ptr<StreamManipulator>(
        make_frame_annotator_stream_manipulator());
  }
}

FrameAnnotatorLoaderStreamManipulator::
    ~FrameAnnotatorLoaderStreamManipulator() {
  stream_manipulator_ = nullptr;
}

bool FrameAnnotatorLoaderStreamManipulator::Initialize(
    GpuResources* gpu_resources,
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  if (stream_manipulator_) {
    return stream_manipulator_->Initialize(gpu_resources, static_info,
                                           result_callback);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  if (stream_manipulator_) {
    return stream_manipulator_->ConfigureStreams(stream_config);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  if (stream_manipulator_) {
    return stream_manipulator_->OnConfiguredStreams(stream_config);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  if (stream_manipulator_) {
    return stream_manipulator_->ConstructDefaultRequestSettings(
        default_request_settings, type);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (stream_manipulator_) {
    return stream_manipulator_->ProcessCaptureRequest(request);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  if (stream_manipulator_) {
    return stream_manipulator_->ProcessCaptureResult(result);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  if (stream_manipulator_) {
    return stream_manipulator_->Notify(msg);
  }
  return true;
}

bool FrameAnnotatorLoaderStreamManipulator::Flush() {
  if (stream_manipulator_) {
    return stream_manipulator_->Flush();
  }
  return true;
}

}  // namespace cros
