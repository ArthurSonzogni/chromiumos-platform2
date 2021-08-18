/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_algorithm_bridge_impl.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"

namespace cros {

// static
std::unique_ptr<CameraAlgorithmBridge> CameraAlgorithmBridge::CreateInstance(
    CameraAlgorithmBackend backend) {
  return CameraAlgorithmBridge::CreateInstance(
      backend, CameraMojoChannelManager::GetInstance());
}

// static
std::unique_ptr<CameraAlgorithmBridge> CameraAlgorithmBridge::CreateInstance(
    CameraAlgorithmBackend backend, CameraMojoChannelManagerToken* token) {
  VLOGF_ENTER();
  return std::make_unique<CameraAlgorithmBridgeImpl>(
      backend, CameraMojoChannelManager::FromToken(token));
}

CameraAlgorithmBridgeImpl::CameraAlgorithmBridgeImpl(
    CameraAlgorithmBackend backend, CameraMojoChannelManager* mojo_manager)
    : mojo_manager_(mojo_manager),
      ipc_bridge_(new IPCBridge(backend, mojo_manager)) {}

CameraAlgorithmBridgeImpl::~CameraAlgorithmBridgeImpl() {
  VLOGF_ENTER();

  bool result = mojo_manager_->GetIpcTaskRunner()->DeleteSoon(
      FROM_HERE, std::move(ipc_bridge_));
  DCHECK(result);

  VLOGF_EXIT();
}

int32_t CameraAlgorithmBridgeImpl::Initialize(
    const camera_algorithm_callback_ops_t* callback_ops) {
  VLOGF_ENTER();

  const int32_t kInitializationRetryTimeoutMs = 3000;
  const int32_t kInitializationWaitConnectionMs = 500;
  const int32_t kInitializationRetrySleepUs = 100000;
  auto get_elapsed_ms = [](struct timespec& start) {
    struct timespec stop = {};
    if (clock_gettime(CLOCK_MONOTONIC, &stop)) {
      LOGF(ERROR) << "Failed to get clock time";
      return 0L;
    }
    return (stop.tv_sec - start.tv_sec) * 1000 +
           (stop.tv_nsec - start.tv_nsec) / 1000000;
  };
  struct timespec start_ts = {};
  if (clock_gettime(CLOCK_MONOTONIC, &start_ts)) {
    LOGF(ERROR) << "Failed to get clock time";
  }
  int ret = 0;
  do {
    int32_t elapsed_ms = get_elapsed_ms(start_ts);
    if (elapsed_ms >= kInitializationRetryTimeoutMs) {
      ret = -ETIMEDOUT;
      break;
    }
    auto future = cros::Future<int32_t>::Create(&relay_);

    mojo_manager_->GetIpcTaskRunner()->PostTask(
        FROM_HERE, base::Bind(&CameraAlgorithmBridgeImpl::IPCBridge::Initialize,
                              ipc_bridge_->GetWeakPtr(), callback_ops,
                              cros::GetFutureCallback(future)));

    if (future->Wait(std::min(kInitializationWaitConnectionMs,
                              kInitializationRetryTimeoutMs - elapsed_ms))) {
      ret = future->Get();
      if (ret == 0 || ret == -EINVAL) {
        break;
      }
    }
    usleep(kInitializationRetrySleepUs);
  } while (1);
  VLOGF_EXIT();
  return ret;
}

int32_t CameraAlgorithmBridgeImpl::RegisterBuffer(int buffer_fd) {
  VLOGF_ENTER();
  auto future = cros::Future<int32_t>::Create(&relay_);

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::IPCBridge::RegisterBuffer,
                 ipc_bridge_->GetWeakPtr(), buffer_fd,
                 cros::GetFutureCallback(future)));

  future->Wait();
  VLOGF_EXIT();
  return future->Get();
}

void CameraAlgorithmBridgeImpl::Request(uint32_t req_id,
                                        const std::vector<uint8_t>& req_header,
                                        int32_t buffer_handle) {
  VLOGF_ENTER();

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::IPCBridge::Request,
                 ipc_bridge_->GetWeakPtr(), req_id, req_header, buffer_handle));

  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::DeregisterBuffers(
    const std::vector<int32_t>& buffer_handles) {
  VLOGF_ENTER();

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::IPCBridge::DeregisterBuffers,
                 ipc_bridge_->GetWeakPtr(), buffer_handles));

  VLOGF_EXIT();
}

CameraAlgorithmBridgeImpl::IPCBridge::IPCBridge(
    CameraAlgorithmBackend backend, CameraMojoChannelManager* mojo_manager)
    : algo_backend_(backend),
      callback_ops_(nullptr),
      mojo_manager_(mojo_manager),
      ipc_task_runner_(mojo_manager_->GetIpcTaskRunner()) {}

CameraAlgorithmBridgeImpl::IPCBridge::~IPCBridge() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();

  Destroy();
}

void CameraAlgorithmBridgeImpl::IPCBridge::Initialize(
    const camera_algorithm_callback_ops_t* callback_ops,
    base::Callback<void(int32_t)> cb) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!callback_ops || !callback_ops->return_callback) {
    cb.Run(-EINVAL);
    return;
  }
  if (cb_impl_) {
    LOGF(WARNING)
        << "Camera algorithm bridge is already initialized. Reinitializing...";
    Destroy();
  }

  constexpr char kGpuAlgoJobFilePath[] = "/etc/init/cros-camera-gpu-algo.conf";
  switch (algo_backend_) {
    case CameraAlgorithmBackend::kVendorCpu:
      remote_ = mojo_manager_->CreateCameraAlgorithmOpsRemote(
          cros::constants::kCrosCameraAlgoSocketPathString, "vendor_cpu");
      break;
    case CameraAlgorithmBackend::kVendorGpu:
      if (!base::PathExists(base::FilePath(kGpuAlgoJobFilePath))) {
        cb.Run(-EINVAL);
        return;
      }
      remote_ = mojo_manager_->CreateCameraAlgorithmOpsRemote(
          cros::constants::kCrosCameraGPUAlgoSocketPathString, "vendor_gpu");
      break;
    case CameraAlgorithmBackend::kGoogleGpu:
      if (!base::PathExists(base::FilePath(kGpuAlgoJobFilePath))) {
        cb.Run(-EINVAL);
        return;
      }
      remote_ = mojo_manager_->CreateCameraAlgorithmOpsRemote(
          cros::constants::kCrosCameraGPUAlgoSocketPathString, "google_gpu");
      break;
    case CameraAlgorithmBackend::kTest:
      remote_ = mojo_manager_->CreateCameraAlgorithmOpsRemote(
          cros::constants::kCrosCameraAlgoSocketPathString, "test");
      break;
  }
  if (!remote_) {
    LOGF(ERROR) << "Failed to connect to the server";
    cb.Run(-EAGAIN);
    return;
  }
  remote_.set_disconnect_handler(base::BindOnce(
      &CameraAlgorithmBridgeImpl::IPCBridge::OnConnectionError, GetWeakPtr()));
  cb_impl_.reset(
      new CameraAlgorithmCallbackOpsImpl(ipc_task_runner_, callback_ops));
  remote_->Initialize(cb_impl_->CreatePendingRemote(), cb);
  callback_ops_ = callback_ops;
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::IPCBridge::RegisterBuffer(
    int buffer_fd, base::Callback<void(int32_t)> cb) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!remote_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    cb.Run(-ECONNRESET);
    return;
  }
  int dup_fd = dup(buffer_fd);
  if (dup_fd < 0) {
    PLOGF(ERROR) << "Failed to dup fd: ";
    cb.Run(-errno);
    return;
  }
  remote_->RegisterBuffer(
      mojo::WrapPlatformFile(base::ScopedPlatformFile(dup_fd)), cb);
}

void CameraAlgorithmBridgeImpl::IPCBridge::Request(
    uint32_t req_id, std::vector<uint8_t> req_header, int32_t buffer_handle) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!remote_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    return;
  }
  remote_->Request(req_id, std::move(req_header), buffer_handle);
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::IPCBridge::DeregisterBuffers(
    std::vector<int32_t> buffer_handles) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!remote_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    return;
  }
  remote_->DeregisterBuffers(std::move(buffer_handles));
}

void CameraAlgorithmBridgeImpl::IPCBridge::OnConnectionError() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(callback_ops_);
  VLOGF_ENTER();
  Destroy();
  if (callback_ops_->notify) {
    callback_ops_->notify(callback_ops_, CAMERA_ALGORITHM_MSG_IPC_ERROR);
  }
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::IPCBridge::Destroy() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (remote_.is_bound()) {
    cb_impl_.reset();
    remote_.reset();
  }
  VLOGF_EXIT();
}

base::WeakPtr<CameraAlgorithmBridgeImpl::IPCBridge>
CameraAlgorithmBridgeImpl::IPCBridge::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

}  // namespace cros
