/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_algorithm_adapter_libcamera.h"

#include <dlfcn.h>

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/notreached.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/system/invitation.h>

#include "cros-camera/camera_algorithm.h"
#include "cros-camera/common.h"

namespace cros {

CameraAlgorithmAdapterLibcamera::CameraAlgorithmAdapterLibcamera()
    : algo_dll_handle_(nullptr), ipc_thread_("IPC thread") {}

CameraAlgorithmAdapterLibcamera::~CameraAlgorithmAdapterLibcamera() = default;

void CameraAlgorithmAdapterLibcamera::Run(base::ScopedFD channel) {
  // VLOGF_ENTER();
  auto future = cros::Future<void>::Create(&relay_);
  ipc_lost_cb_ = cros::GetFutureCallback(future);
  ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraAlgorithmAdapterLibcamera::InitializeOnIpcThread,
                     base::Unretained(this), std::move(channel)));

  future->Wait(-1);
  exit(EXIT_SUCCESS);
}

void CameraAlgorithmAdapterLibcamera::InitializeOnIpcThread(
    base::ScopedFD channel) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  const char* algo_lib_name = "libcamera_ipa_proxy.so";
  algo_dll_handle_ = dlopen(algo_lib_name, RTLD_NOW | RTLD_GLOBAL);
  if (!algo_dll_handle_) {
    LOGF(ERROR) << "Failed to dlopen: " << dlerror();
    DestroyOnIpcThread();
    return;
  }

  void* symbol = dlsym(algo_dll_handle_, "ipaRun");
  if (!symbol) {
    LOGF(ERROR) << "Camera algorithm is invalid: " << dlerror();
    DestroyOnIpcThread();
    return;
  }

  typedef int (*IPARun)(int);
  auto ipaRun_ = reinterpret_cast<IPARun>(symbol);

  int fdInt = channel.release();
  VLOGF(1) << "Camera algorithm start running";
  int ret = ipaRun_(fdInt);

  VLOGF(1) << "Camera algorithm finished. Ret: " << ret;

  DestroyOnIpcThread();
}

void CameraAlgorithmAdapterLibcamera::DestroyOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  if (algo_dll_handle_) {
    dlclose(algo_dll_handle_);
  }
  std::move(ipc_lost_cb_).Run();
}

}  // namespace cros
