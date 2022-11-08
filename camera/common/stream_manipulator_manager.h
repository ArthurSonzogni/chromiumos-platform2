/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_STREAM_MANIPULATOR_MANAGER_H_
#define CAMERA_COMMON_STREAM_MANIPULATOR_MANAGER_H_

#include <hardware/camera3.h>

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>

#include "common/camera_hal3_helpers.h"
#include "common/camera_metadata_inspector.h"
#include "common/stream_manipulator.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "cros-camera/export.h"
#include "gpu/gpu_resources.h"

namespace cros {

class CROS_CAMERA_EXPORT StreamManipulatorManager {
 public:
  StreamManipulatorManager(StreamManipulator::Options options,
                           StreamManipulator::RuntimeOptions* runtime_options,
                           GpuResources* gpu_resources,
                           CameraMojoChannelManagerToken* mojo_manager_token);
  ~StreamManipulatorManager() = default;

  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::CaptureResultCallback result_callback);
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config);
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type);
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request);
  bool Flush();
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result);
  bool Notify(camera3_notify_msg_t* msg);

 private:
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators_;

  // The metadata inspector to dump capture requests / results in realtime
  // for debugging if enabled.
  std::unique_ptr<CameraMetadataInspector> camera_metadata_inspector_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_STREAM_MANIPULATOR_MANAGER_H_
