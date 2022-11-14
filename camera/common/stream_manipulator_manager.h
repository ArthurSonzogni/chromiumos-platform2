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
  struct CreateOptions {
    // Used to identify the camera device that the stream manipulators will be
    // created for (e.g. USB v.s. vendor camera HAL).
    std::string camera_module_name;

    // Used by the face detection stream manipulator to provide a callback for
    // camera HAL.
    base::OnceCallback<void(FaceDetectionResultCallback)>
        set_face_detection_result_callback;
  };

  StreamManipulatorManager(CreateOptions create_options,
                           StreamManipulator::RuntimeOptions* runtime_options,
                           GpuResources* gpu_resources,
                           CameraMojoChannelManagerToken* mojo_manager_token);
  explicit StreamManipulatorManager(
      std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators);
  ~StreamManipulatorManager() = default;

  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::CaptureResultCallback result_callback);
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config);
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type);
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request);
  bool Flush();
  void ProcessCaptureResult(Camera3CaptureDescriptor result);
  bool Notify(camera3_notify_msg_t* msg);

 private:
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators_;

  // The metadata inspector to dump capture requests / results in realtime
  // for debugging if enabled.
  std::unique_ptr<CameraMetadataInspector> camera_metadata_inspector_;

  // A callback to return the capture result to the framework.
  StreamManipulator::CaptureResultCallback result_callback_;

  // A thread where StreamManipulator::ProcessCaptureResult() runs if
  // StreamManipulator does not specify a thread for the task via
  // StreamManipulator::GetTaskRunner().
  base::Thread default_capture_result_thread_;

  // A callback that is called by StreamManipulator to call the next
  // StreamManipulator::ProcessCaptureResult(). |stream_manipulator_index| is
  // the index of the StreamManipulator that is going to process |result|.
  // |stream_manipulator_index| is bound by StreamManipulatorManager.
  void ProcessCaptureResultOnStreamManipulator(int stream_manipulator_index,
                                               Camera3CaptureDescriptor result);

  // A callback that is called by StreamManipulator to return the capture result
  // to the framework.
  void ReturnResultToClient(Camera3CaptureDescriptor result);

  // For the meaning of |position|, see the comment of CameraMetadataInspector's
  // |inspect_positions_|.
  void InspectResult(int position, Camera3CaptureDescriptor& result);
};

}  // namespace cros

#endif  // CAMERA_COMMON_STREAM_MANIPULATOR_MANAGER_H_
