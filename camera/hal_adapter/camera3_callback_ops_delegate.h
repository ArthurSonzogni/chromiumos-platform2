/*
 * Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_

#include <vector>

#include <hardware/camera3.h>

#include "camera/mojo/camera3.mojom.h"
#include "common/utils/cros_camera_mojo_utils.h"

namespace cros {

class CameraDeviceAdapter;

class Camera3CallbackOpsDelegate
    : public internal::MojoRemote<mojom::Camera3CallbackOps> {
 public:
  explicit Camera3CallbackOpsDelegate(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  Camera3CallbackOpsDelegate(const Camera3CallbackOpsDelegate&) = delete;
  Camera3CallbackOpsDelegate& operator=(const Camera3CallbackOpsDelegate&) =
      delete;

  ~Camera3CallbackOpsDelegate() = default;

  void ProcessCaptureResult(mojom::Camera3CaptureResultPtr result);

  void Notify(mojom::Camera3NotifyMsgPtr msg);

  void RequestStreamBuffers(
      std::vector<mojom::Camera3BufferRequestPtr> buffer_reqs,
      mojom::Camera3CallbackOps::RequestStreamBuffersCallback cb);

  void ReturnStreamBuffers(std::vector<mojom::Camera3StreamBufferPtr> buffers);

 private:
  void ProcessCaptureResultOnThread(mojom::Camera3CaptureResultPtr result);

  void NotifyOnThread(mojom::Camera3NotifyMsgPtr msg);

  void RequestStreamBuffersOnThread(
      std::vector<mojom::Camera3BufferRequestPtr> buffer_reqs,
      mojom::Camera3CallbackOps::RequestStreamBuffersCallback cb);

  void ReturnStreamBuffersOnThread(
      std::vector<mojom::Camera3StreamBufferPtr> buffers);
};

}  // end of namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_
