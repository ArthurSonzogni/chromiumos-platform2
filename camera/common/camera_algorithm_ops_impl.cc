/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_algorithm_ops_impl.h"

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/future.h"

namespace cros {

CameraAlgorithmOpsImpl* CameraAlgorithmOpsImpl::singleton_ = nullptr;

CameraAlgorithmOpsImpl::CameraAlgorithmOpsImpl()
    : receiver_(this), cam_algo_(nullptr) {
  singleton_ = this;
}

// static
CameraAlgorithmOpsImpl* CameraAlgorithmOpsImpl::GetInstance() {
  static base::NoDestructor<CameraAlgorithmOpsImpl> impl;
  return impl.get();
}

bool CameraAlgorithmOpsImpl::Bind(
    mojo::PendingReceiver<mojom::CameraAlgorithmOps> pending_receiver,
    camera_algorithm_ops_t* cam_algo,
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    const base::Closure& ipc_lost_handler) {
  DCHECK(ipc_task_runner->BelongsToCurrentThread());
  if (receiver_.is_bound()) {
    LOGF(ERROR) << "Algorithm Ops is already bound";
    return false;
  }
  DCHECK(!cam_algo_);
  DCHECK(!ipc_task_runner_);
  DCHECK(!callback_ops_.is_bound());
  receiver_.Bind(std::move(pending_receiver));
  cam_algo_ = cam_algo;
  ipc_task_runner_ = std::move(ipc_task_runner);
  receiver_.set_disconnect_handler(ipc_lost_handler);
  return true;
}

void CameraAlgorithmOpsImpl::Unbind() {
  DCHECK(ipc_task_runner_);
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(receiver_.is_bound());
  DCHECK(cam_algo_);
  callback_ops_.reset();
  ipc_task_runner_ = nullptr;
  cam_algo_ = nullptr;
  if (receiver_.is_bound()) {
    receiver_.reset();
  }
}

void CameraAlgorithmOpsImpl::Initialize(
    mojo::PendingRemote<mojom::CameraAlgorithmCallbackOps> callback_ops,
    InitializeCallback callback) {
  DCHECK(cam_algo_);
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(callback_ops.is_valid());
  VLOGF_ENTER();
  int32_t result = 0;
  if (callback_ops_.is_bound()) {
    LOGF(ERROR) << "Return callback is already registered";
    std::move(callback).Run(-EINVAL);
    return;
  }
  CameraAlgorithmOpsImpl::return_callback =
      CameraAlgorithmOpsImpl::ReturnCallbackForwarder;
  result = cam_algo_->initialize(this);
  callback_ops_.Bind(std::move(callback_ops));
  std::move(callback).Run(result);
  VLOGF_EXIT();
}

void CameraAlgorithmOpsImpl::RegisterBuffer(mojo::ScopedHandle buffer_fd,
                                            RegisterBufferCallback callback) {
  DCHECK(cam_algo_);
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  base::ScopedPlatformFile fd;
  MojoResult mojo_result = mojo::UnwrapPlatformFile(std::move(buffer_fd), &fd);
  if (mojo_result != MOJO_RESULT_OK) {
    LOGF(ERROR) << "Failed to unwrap handle: " << mojo_result;
    std::move(callback).Run(-EBADF);
    return;
  }
  int32_t result = cam_algo_->register_buffer(fd.release());
  std::move(callback).Run(result);
  VLOGF_EXIT();
}

void CameraAlgorithmOpsImpl::Request(uint32_t req_id,
                                     const std::vector<uint8_t>& req_header,
                                     int32_t buffer_handle) {
  DCHECK(cam_algo_);
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!callback_ops_.is_bound()) {
    LOGF(ERROR) << "Return callback is not registered yet";
    return;
  }
  // TODO(b/37434548): This can be removed after libchrome uprev.
  const std::vector<uint8_t>& header = req_header;
  cam_algo_->request(req_id, header.data(), header.size(), buffer_handle);
  VLOGF_EXIT();
}

void CameraAlgorithmOpsImpl::DeregisterBuffers(
    const std::vector<int32_t>& buffer_handles) {
  DCHECK(cam_algo_);
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  // TODO(b/37434548): This can be removed after libchrome uprev.
  const std::vector<int32_t>& handles = buffer_handles;
  cam_algo_->deregister_buffers(handles.data(), handles.size());
  VLOGF_EXIT();
}

// static
void CameraAlgorithmOpsImpl::ReturnCallbackForwarder(
    const camera_algorithm_callback_ops_t* callback_ops,
    uint32_t req_id,
    uint32_t status,
    int32_t buffer_handle) {
  VLOGF_ENTER();
  if (const_cast<CameraAlgorithmOpsImpl*>(
          static_cast<const CameraAlgorithmOpsImpl*>(callback_ops)) !=
      singleton_) {
    LOGF(ERROR) << "Invalid callback ops provided";
    return;
  }
  singleton_->ipc_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmOpsImpl::ReturnCallbackOnIPCThread,
                 base::Unretained(singleton_), req_id, status, buffer_handle));
}

void CameraAlgorithmOpsImpl::ReturnCallbackOnIPCThread(uint32_t req_id,
                                                       uint32_t status,
                                                       int32_t buffer_handle) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!callback_ops_.is_bound()) {
    LOGF(WARNING) << "Callback is not bound. IPC broken?";
  } else {
    callback_ops_->Return(req_id, status, buffer_handle);
  }
  VLOGF_EXIT();
}

}  // namespace cros
