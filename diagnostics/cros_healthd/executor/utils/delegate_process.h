// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DELEGATE_PROCESS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DELEGATE_PROCESS_H_

#include <string>
#include <vector>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>

#include "diagnostics/cros_healthd/executor/mojom/delegate.mojom.h"
#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"

namespace diagnostics {

// Run executor delegate.
//
// Argument definition can be found in the following header:
//     cros_healthd/executor/utils/sandboxed_process.h.
//
// Notice:
// 1. |DelegateProcess| can't be initialized when the current thread is dealing
// with a mojo task. For example, when calling `remote->Func()`, we can't create
// |DelegateProcess| in `Func()`. A recommended way to create it is to launch a
// new task in same thread.
//
// 2. The users should be aware of the lifecycle of this object. Once it's
// destroyed, the mojo connection to the delegate will disconnect.
//
// Example usage:
//   // Assume `Func()` is the mojo receiver interface in the executor.
//   void Executor::Func(callback) {
//     base::SequencedTaskRunnerHandle::Get()->PostTask(
//         FROM_HERE, base::BindOnce(&FuncTask, std::move(callback)));
//   }
//
//   void FuncTask(base::OnceCallback<void(mojom::XXXResultPtr)> callback) {
//     auto delegate = std::make_unique<DelegateProcess>("seccomp.policy");
//     auto cb = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
//         std::move(callback), default error params);
//
//     // Remember to move the |delegate| in to prevent being destroyed.
//     delegate->remote()->GetData(
//         base::BindOnce(&GetDataCallback, std::move(cb),
//         std::move(delegate)));
//   }
class DelegateProcess : public SandboxedProcess {
 public:
  using SandboxedProcess::SandboxedProcess;
  DelegateProcess(const std::string& seccomp_filename,
                  const std::string& user,
                  uint64_t capabilities_mask,
                  const std::vector<base::FilePath>& readonly_mount_points,
                  const std::vector<base::FilePath>& writable_mount_points);
  DelegateProcess(
      const std::string& seccomp_filename,
      const std::vector<base::FilePath>& readonly_mount_points = {});

  DelegateProcess(const DelegateProcess&) = delete;
  DelegateProcess& operator=(const DelegateProcess&) = delete;
  ~DelegateProcess() override;

  auto remote() { return remote_.get(); }

 protected:
  DelegateProcess();

 private:
  virtual void SendMojoInvitation();
  virtual void RunDelegate();

  mojo::PlatformChannel channel_;
  mojo::Remote<ash::cros_healthd::mojom::Delegate> remote_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_DELEGATE_PROCESS_H_
