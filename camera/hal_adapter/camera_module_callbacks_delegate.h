/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_MODULE_CALLBACKS_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_CAMERA_MODULE_CALLBACKS_DELEGATE_H_

#include "common/utils/cros_camera_mojo_utils.h"
#include "cros-camera/future.h"
#include "mojo/camera_common.mojom.h"

namespace cros {

class CameraModuleCallbacksDelegate
    : public internal::MojoChannel<mojom::CameraModuleCallbacks> {
 public:
  CameraModuleCallbacksDelegate(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  ~CameraModuleCallbacksDelegate() = default;

  void CameraDeviceStatusChange(int camera_id, int new_status);

  void TorchModeStatusChange(int camera_id, int new_status);

 private:
  void CameraDeviceStatusChangeOnThread(int camera_id,
                                        int new_status,
                                        base::Closure callback);

  void TorchModeStatusChangeOnThread(int camera_id,
                                     int new_status,
                                     base::Closure callback);

  cros::CancellationRelay relay_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CameraModuleCallbacksDelegate);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_MODULE_CALLBACKS_DELEGATE_H_
