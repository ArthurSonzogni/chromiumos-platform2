/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <utility>

#include "common/camera_algorithm_callback_ops_impl.h"

#include "cros-camera/common.h"

#include <base/check.h>

namespace cros {

CameraAlgorithmCallbackOpsImpl::CameraAlgorithmCallbackOpsImpl(
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    const camera_algorithm_callback_ops_t* callback_ops)
    : receiver_(this),
      ipc_task_runner_(std::move(ipc_task_runner)),
      callback_ops_(callback_ops) {}

void CameraAlgorithmCallbackOpsImpl::Return(uint32_t req_id,
                                            uint32_t status,
                                            int32_t buffer_handle) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(callback_ops_);
  DCHECK(callback_ops_->return_callback);
  VLOGF_ENTER();
  callback_ops_->return_callback(callback_ops_, req_id, status, buffer_handle);
  VLOGF_EXIT();
}

mojo::PendingRemote<mojom::CameraAlgorithmCallbackOps>
CameraAlgorithmCallbackOpsImpl::CreatePendingRemote() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  return receiver_.BindNewPipeAndPassRemote();
}

}  // namespace cros
