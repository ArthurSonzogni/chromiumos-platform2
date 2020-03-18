/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/camera_service_connector_impl.h"

#include <errno.h>
#include <utility>

#include <base/no_destructor.h>
#include <base/sequence_checker.h>
#include <base/synchronization/waitable_event.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/types.h"
#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"

namespace cros {

CameraServiceConnector::CameraServiceConnector()
    : ipc_thread_("CamConn"), camera_client_(nullptr) {}

CameraServiceConnector* CameraServiceConnector::GetInstance() {
  static base::NoDestructor<CameraServiceConnector> instance;
  return instance.get();
}

void CameraServiceConnector::Init(IntOnceCallback init_callback) {
  VLOGF_ENTER();
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  mojo::core::Init();
  bool ret = ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
  if (!ret) {
    LOGF(ERROR) << "Failed to start IPC thread";
    std::move(init_callback).Run(-ENODEV);
    return;
  }
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraServiceConnector::InitOnThread,
                     base::Unretained(this), std::move(init_callback)));
}

void CameraServiceConnector::Exit() {
  VLOGF_ENTER();
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  camera_client_->Exit();

  ipc_support_ = nullptr;
  ipc_thread_.Stop();
}

void CameraServiceConnector::RegisterClient(
    mojom::CameraHalClientPtr camera_hal_client) {
  VLOGF_ENTER();
  // This may be called from a different thread than the main thread,
  // (for example here it is called from CameraClient thread),
  // but mojo operations have to run on the same thread that bound
  // the interface, so we bounce the request over to that thread/runner.
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraServiceConnector::RegisterClientOnThread,
                     base::Unretained(this), std::move(camera_hal_client)));
}

void CameraServiceConnector::RegisterClientOnThread(
    mojom::CameraHalClientPtr camera_hal_client) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  dispatcher_->RegisterClient(std::move(camera_hal_client));
}

void CameraServiceConnector::InitOnThread(IntOnceCallback init_callback) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  mojo::ScopedMessagePipeHandle child_pipe;
  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  MojoResult res =
      CreateMojoChannelToParentByUnixDomainSocket(socket_path, &child_pipe);
  if (res != MOJO_RESULT_OK) {
    LOGF(ERROR) << "Failed to create mojo channel to dispatcher";
    std::move(init_callback).Run(-ENODEV);
    return;
  }

  dispatcher_ = mojo::MakeProxy(
      mojom::CameraHalDispatcherPtrInfo(std::move(child_pipe), 0u),
      ipc_thread_.task_runner());
  bool connected = dispatcher_.is_bound();
  if (!connected) {
    LOGF(ERROR) << "Failed to make a proxy to dispatcher";
    std::move(init_callback).Run(-ENODEV);
    return;
  }
  dispatcher_.set_connection_error_handler(base::BindOnce(
      &CameraServiceConnector::OnDispatcherError, base::Unretained(this)));
  LOGF(INFO) << "Dispatcher connected";

  camera_client_ = std::make_unique<CameraClient>();
  camera_client_->Init(base::BindOnce(&CameraServiceConnector::RegisterClient,
                                      base::Unretained(this)),
                       std::move(init_callback));
}

void CameraServiceConnector::OnDispatcherError() {
  VLOGF_ENTER();
  // TODO(b/151047930): Attempt to reconnect on dispatcher error.
  LOGF(FATAL) << "Connection to camera dispatcher lost";
}

}  // namespace cros
