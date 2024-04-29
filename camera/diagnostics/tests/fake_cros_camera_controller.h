// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_TESTS_FAKE_CROS_CAMERA_CONTROLLER_H_
#define CAMERA_DIAGNOSTICS_TESTS_FAKE_CROS_CAMERA_CONTROLLER_H_

#include <vector>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "common/utils/camera_mojo_service_provider.h"

namespace cros::tests {

enum FrameType {
  kAny = 0,
  kBlack,
  kBlurry,
  kGreen,
};

class FakeCrosCameraController final
    : public camera_diag::mojom::CrosCameraController {
 public:
  explicit FakeCrosCameraController(
      mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceManager>
          service_manager);
  FakeCrosCameraController(const FakeCrosCameraController&) = delete;
  FakeCrosCameraController& operator=(const FakeCrosCameraController&) = delete;
  ~FakeCrosCameraController() final = default;

  void Initialize();

  void OpenCamera(camera_diag::mojom::CameraStreamPtr stream,
                  FrameType frame_type = FrameType::kAny);

 private:
  bool ValidateDiagnosticsFrame(
      const camera_diag::mojom::CameraFramePtr& frame);

  void FillFrame(camera_diag::mojom::CameraFramePtr& frame,
                 const FrameType& frame_type);

  //
  // Implementation of camera_diag::mojom::CrosCameraController.
  //
  void StartStreaming(camera_diag::mojom::StreamingConfigPtr config,
                      StartStreamingCallback callback) final;

  void StopStreaming() final;

  void RequestFrame(camera_diag::mojom::CameraFramePtr frame) final;

  void SendFrame(camera_diag::mojom::CameraFramePtr frame);

  cros::internal::CameraMojoServiceProvider<
      camera_diag::mojom::CrosCameraController>
      cros_camera_provider_{this};

  mojo::Remote<camera_diag::mojom::CrosCameraDiagnosticsService> diag_service_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  camera_diag::mojom::CameraStreamPtr stream_ = nullptr;

  FrameType frame_type_ = FrameType::kAny;

  std::vector<uint8_t> cached_nv12_data_;

  int frame_interval_ = 10;

  int next_frame_number_ = 0;
};
}  // namespace cros::tests

#endif  // CAMERA_DIAGNOSTICS_TESTS_FAKE_CROS_CAMERA_CONTROLLER_H_
