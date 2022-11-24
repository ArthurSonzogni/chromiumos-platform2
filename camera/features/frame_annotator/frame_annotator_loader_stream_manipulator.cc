/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/frame_annotator/frame_annotator_loader_stream_manipulator.h"

#include <utility>

#include <dlfcn.h>

#include <base/files/file_util.h>

#include "features/frame_annotator/libs/utils.h"

namespace cros {

namespace {

constexpr std::array<const char*, 4> kFrameAnnotatorLibPath = {
    // Check rootfs first for ease of local development.
    "/usr/lib64/libcros_camera_frame_annotator.so",
    "/usr/lib/libcros_camera_frame_annotator.so",

    // By default the .so is installed in the stateful partition on test images.
    "/usr/local/lib64/libcros_camera_frame_annotator.so",
    "/usr/local/lib/libcros_camera_frame_annotator.so",
};

}  // namespace

//
// FrameAnnotatorLoaderStreamManipulator implementations.
//

FrameAnnotatorLoaderStreamManipulator::FrameAnnotatorLoaderStreamManipulator() {
  for (auto* p : kFrameAnnotatorLibPath) {
    if (base::PathExists(base::FilePath(p))) {
      auto native_lib = base::ScopedNativeLibrary(base::FilePath(p));
      if (auto make_frame_annotator_stream_manipulator =
              reinterpret_cast<decltype(&MakeFrameAnnotatorStreamManipulator)>(
                  native_lib.GetFunctionPointer(
                      "MakeFrameAnnotatorStreamManipulator"))) {
        stream_manipulator_ = std::unique_ptr<StreamManipulator>(
            make_frame_annotator_stream_manipulator());
        frame_annotator_lib_ = std::move(native_lib);
        LOGF(INFO) << "FrameAnnotatorLoaderStreamManipulator loaded from " << p;
        break;
      } else {
        LOGF(INFO)
            << "Failed to load FrameAnnotatorLoaderStreamManipulator from " << p
            << " with error: " << native_lib.GetError()->ToString();
      }
    }
  }
}

FrameAnnotatorLoaderStreamManipulator::
    ~FrameAnnotatorLoaderStreamManipulator() {
  stream_manipulator_ = nullptr;
}

bool FrameAnnotatorLoaderStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  if (stream_manipulator_) {
    return stream_manipulator_->Initialize(static_info,
                                           std::move(result_callback));
  }
  result_callback_ = std::move(result_callback);
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
    Camera3CaptureDescriptor result) {
  if (stream_manipulator_) {
    return stream_manipulator_->ProcessCaptureResult(std::move(result));
  }
  result_callback_.Run(std::move(result));
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
