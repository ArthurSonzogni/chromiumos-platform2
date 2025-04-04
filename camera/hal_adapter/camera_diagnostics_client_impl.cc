// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hal_adapter/camera_diagnostics_client_impl.h"

#include <optional>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/no_destructor.h>
#include <base/synchronization/lock.h>
#include <base/task/sequenced_task_runner.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/common.h"

namespace cros {

CrosCameraDiagnosticsService::CrosCameraDiagnosticsService(
    CameraMojoChannelManager* mojo_manager)
    : internal::MojoRemote<camera_diag::mojom::CrosCameraDiagnosticsService>(
          mojo_manager->GetIpcTaskRunner()),
      mojo_manager_(mojo_manager) {}

void CrosCameraDiagnosticsService::SendFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CrosCameraDiagnosticsService::SendFrameOnThread,
                     base::Unretained(this), std::move(frame)));
}

void CrosCameraDiagnosticsService::SendFrameOnThread(
    camera_diag::mojom::CameraFramePtr frame) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (!remote_.is_bound() || !remote_.is_connected()) {
    Connect();
  }
  remote_->SendFrame(std::move(frame));
}

void CrosCameraDiagnosticsService::Connect() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  mojo_manager_->RequestServiceFromMojoServiceManager(
      chromeos::mojo_services::kCrosCameraDiagnosticsService,
      remote_.BindNewPipeAndPassReceiver().PassPipe());
  remote_.set_disconnect_handler(base::BindOnce(
      &CrosCameraDiagnosticsService::OnDisconnect, base::Unretained(this)));
}

void CrosCameraDiagnosticsService::OnDisconnect() {
  remote_.reset();
  LOGF(INFO) << "Disconnected from "
             << chromeos::mojo_services::kCrosCameraDiagnosticsService;
}

CameraDiagnosticsClientImpl::CameraDiagnosticsClientImpl(
    CameraMojoChannelManager* mojo_manager)
    : diagnostics_service_(mojo_manager),
      ipc_task_runner_(mojo_manager->GetIpcTaskRunner()) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  service_provider_.Register(mojo_manager->GetServiceManagerProxy(),
                             chromeos::mojo_services::kCrosCameraController);
}

CameraDiagnosticsClientImpl::~CameraDiagnosticsClientImpl() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  service_provider_.Reset();
}

void CameraDiagnosticsClientImpl::SendFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  diagnostics_service_.SendFrame(std::move(frame));
}

bool CameraDiagnosticsClientImpl::IsFrameAnalysisEnabled() {
  return frame_analysis_enabled_;
}

uint32_t CameraDiagnosticsClientImpl::frame_interval() const {
  return frame_interval_;
}

std::optional<camera_diag::mojom::CameraFramePtr>
CameraDiagnosticsClientImpl::RequestEmptyFrame() {
  base::AutoLock lock(session_lock_);
  if (frame_list_.empty()) {
    return std::nullopt;
  }
  auto frame = std::move(frame_list_.back());
  frame_list_.pop_back();
  return frame;
}

void CameraDiagnosticsClientImpl::AddCameraSession(const Size& stream_size) {
  base::AutoLock lock(session_lock_);
  if (session_stream_size_.has_value()) {
    LOGF(ERROR) << "Diagnostics session is already running!";
    return;
  }
  session_stream_size_ = stream_size;
}

void CameraDiagnosticsClientImpl::RemoveCameraSession() {
  base::AutoLock lock(session_lock_);
  session_stream_size_.reset();
  frame_interval_ = 0;
  frame_analysis_enabled_ = false;
  frame_list_.clear();
  // TODO(imranziad): Inform the diagnostics service.
}

void CameraDiagnosticsClientImpl::StartStreaming(
    camera_diag::mojom::StreamingConfigPtr config,
    StartStreamingCallback callback) {
  VLOGF(1) << "StartStreaming called";
  camera_diag::mojom::StartStreamingResultPtr result;

  {
    // Don't run the IPC callback while holding the lock. Diagnostics
    // service can call another endpoint again in that callback and
    // create a deadlock.
    base::AutoLock lock(session_lock_);
    frame_analysis_enabled_ = false;
    frame_list_.clear();

    if (!session_stream_size_.has_value()) {
      result = camera_diag::mojom::StartStreamingResult::NewError(
          camera_diag::mojom::ErrorCode::kCameraClosed);
    } else {
      auto stream = camera_diag::mojom::CameraStream::New();
      stream->height = session_stream_size_.value().height;
      stream->width = session_stream_size_.value().width;
      // Only supported format for now.
      stream->pixel_format = camera_diag::mojom::PixelFormat::kYuv420;
      result = camera_diag::mojom::StartStreamingResult::NewStream(
          std::move(stream));
      frame_interval_ = config->frame_interval;
      frame_analysis_enabled_ = true;
    }
  }

  std::move(callback).Run(std::move(result));
}

void CameraDiagnosticsClientImpl::StopStreaming() {
  VLOGF(1) << "StopStreaming called";
  frame_analysis_enabled_ = false;
  base::AutoLock lock(session_lock_);
  // Drop the frames, no need to send them back.
  frame_list_.clear();
}

void CameraDiagnosticsClientImpl::RequestFrame(
    camera_diag::mojom::CameraFramePtr frame) {
  base::AutoLock lock(session_lock_);
  if (session_stream_size_.has_value() && frame_analysis_enabled_) {
    VLOGF(1) << "Received frame, size " << frame->buffer->size;
    frame_list_.push_back(std::move(frame));
  } else {
    // Rejection; send the frame back.
    SendFrame(std::move(frame));
  }
}

}  // namespace cros
