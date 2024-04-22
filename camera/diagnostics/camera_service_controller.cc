// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/camera_service_controller.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <chromeos/mojo/service_constants.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"

namespace cros {

CameraServiceController::CameraServiceController(
    CameraDiagnosticsMojoManager* mojo_manager)
    : mojo_manager_(mojo_manager),
      ipc_task_runner_(mojo_manager->GetTaskRunner()) {}

CameraServiceController::~CameraServiceController() {
  if (ipc_task_runner_->RunsTasksInCurrentSequence()) {
    ResetRemote(base::NullCallback());
  } else {
    auto future = cros::Future<void>::Create(nullptr);
    ipc_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&CameraServiceController::ResetRemote,
                                  base::Unretained(this),
                                  cros::GetFutureCallback(future)));
    future->Wait();
  }
}

void CameraServiceController::StartStreaming(
    camera_diag::mojom::StreamingConfigPtr config,
    CameraStartStreamingCallback callback) {
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraServiceController::InitiateStartStreaming,
                     base::Unretained(this), std::move(config),
                     std::move(callback)));
}

void CameraServiceController::StopStreaming() {
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&CameraServiceController::StopStreamingInternal,
                                base::Unretained(this)));
}

void CameraServiceController::RequestFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&CameraServiceController::RequestFrameInternal,
                                base::Unretained(this), std::move(frame)));
}

//
// InitiateStartStreaming() flow:
//
// 1. Connected to remote:
//    - Start streaming: remote_->StartStreaming()
//
// 2. Not connected:
//    - Query camera service status:
//        - Unregistered: Error (kCrosCameraControllerNotRegistered)
//        - Registered:
//            - Request remote from MojoServiceManager
//            - Start streaming: remote_->StartStreaming(StartStreamingCallback)
//                - StartStreamingCallback handles:
//                    - Error (e.g., kCameraClosed)
//                    - Success (CameraStreamConfig)
//
void CameraServiceController::InitiateStartStreaming(
    camera_diag::mojom::StreamingConfigPtr config,
    CameraStartStreamingCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  if (remote_.is_bound() && remote_.is_connected()) {
    remote_->StartStreaming(std::move(config), std::move(callback));
    return;
  }

  auto start_streaming_callback = base::BindOnce(
      &CameraServiceController::StartStreamingInternal, base::Unretained(this),
      std::move(config), std::move(callback));

  mojo_manager_->GetMojoServiceManager()->Query(
      chromeos::mojo_services::kCrosCameraController,
      std::move(start_streaming_callback));
}

void CameraServiceController::StartStreamingInternal(
    camera_diag::mojom::StreamingConfigPtr config,
    CameraStartStreamingCallback callback,
    chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
        err_or_state) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  bool is_registered = err_or_state && err_or_state->is_state() &&
                       err_or_state->get_state()->is_registered_state();
  if (!is_registered) {
    std::move(callback).Run(camera_diag::mojom::StartStreamingResult::NewError(
        camera_diag::mojom::ErrorCode::kCrosCameraControllerNotRegistered));
    return;
  }

  mojo_manager_->GetMojoServiceManager()->Request(
      chromeos::mojo_services::kCrosCameraController, /*timeout=*/std::nullopt,
      remote_.BindNewPipeAndPassReceiver().PassPipe());
  LOGF(INFO) << "Connected to camera service";

  remote_->StartStreaming(std::move(config), std::move(callback));
}

void CameraServiceController::StopStreamingInternal() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  if (remote_.is_bound()) {
    remote_->StopStreaming();
  }
}

void CameraServiceController::RequestFrameInternal(
    camera_diag::mojom::CameraFramePtr frame) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  remote_->RequestFrame(std::move(frame));
}

void CameraServiceController::ResetRemote(base::OnceClosure callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  remote_.reset();
  if (callback) {
    std::move(callback).Run();
  }
}

}  // namespace cros
