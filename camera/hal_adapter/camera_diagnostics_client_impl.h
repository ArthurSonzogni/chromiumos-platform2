// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_IMPL_H_
#define CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_IMPL_H_

#include <list>
#include <optional>

#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>
#include <base/task/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "common/camera_diagnostics_client.h"
#include "common/utils/camera_mojo_service_provider.h"
#include "common/utils/cros_camera_mojo_utils.h"
#include "cros-camera/camera_mojo_channel_manager.h"

namespace cros {

class CrosCameraDiagnosticsService
    : public internal::MojoRemote<
          camera_diag::mojom::CrosCameraDiagnosticsService> {
 public:
  explicit CrosCameraDiagnosticsService(CameraMojoChannelManager* mojo_manager);
  CrosCameraDiagnosticsService(const CrosCameraDiagnosticsService&) = delete;
  CrosCameraDiagnosticsService& operator=(const CrosCameraDiagnosticsService&) =
      delete;

  void SendFrame(camera_diag::mojom::CameraFramePtr frame);

 private:
  void SendFrameOnThread(camera_diag::mojom::CameraFramePtr);

  void Connect();

  void OnDisconnect();

  CameraMojoChannelManager* mojo_manager_;
};

// A wrapper for mojo call to camera diagnostics service.
// Not thread-safe; must be created and destroyed in IPC thread.
class CameraDiagnosticsClientImpl final
    : public CameraDiagnosticsClient,
      public camera_diag::mojom::CrosCameraController {
 public:
  explicit CameraDiagnosticsClientImpl(CameraMojoChannelManager* mojo_manager);
  CameraDiagnosticsClientImpl(const CameraDiagnosticsClientImpl&) = delete;
  CameraDiagnosticsClientImpl& operator=(const CameraDiagnosticsClientImpl&) =
      delete;
  CameraDiagnosticsClientImpl(CameraDiagnosticsClientImpl&&) = delete;
  CameraDiagnosticsClientImpl& operator=(CameraDiagnosticsClientImpl&&) =
      delete;
  ~CameraDiagnosticsClientImpl() final;

  //
  // Implementation of CameraDiagnosticsClient.
  //

  bool IsFrameAnalysisEnabled() final;

  uint32_t frame_interval() const final;

  std::optional<camera_diag::mojom::CameraFramePtr> RequestEmptyFrame() final;

  void AddCameraSession(const Size& stream_size) final;

  void RemoveCameraSession() final;

  void SendFrame(camera_diag::mojom::CameraFramePtr frame) final;

  //
  // Implementation of camera_diag::mojom::CrosCameraController.
  //

  void StartStreaming(camera_diag::mojom::StreamingConfigPtr config,
                      StartStreamingCallback callback) final;

  void StopStreaming() final;

  void RequestFrame(camera_diag::mojom::CameraFramePtr frame) final;

 private:
  CrosCameraDiagnosticsService diagnostics_service_;

  scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;

  internal::CameraMojoServiceProvider<camera_diag::mojom::CrosCameraController>
      service_provider_{this};

  base::Lock session_lock_;
  // |session_stream_size_| holds the selected stream for diagnosis when a
  // camera session is in progress.
  std::optional<Size> session_stream_size_ GUARDED_BY(session_lock_);

  std::atomic<bool> frame_analysis_enabled_ = false;

  uint32_t frame_interval_;

  // Empty frames sent by camera diagnostics.
  std::list<camera_diag::mojom::CameraFramePtr> frame_list_
      GUARDED_BY(session_lock_);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_IMPL_H_
