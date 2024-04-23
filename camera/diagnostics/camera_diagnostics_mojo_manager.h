// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_MOJO_MANAGER_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_MOJO_MANAGER_H_

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

namespace cros {

// Current thread will be considered as IPC thread.
// The object will need to be destroyed in the same thread.
class CameraDiagnosticsMojoManager {
 public:
  CameraDiagnosticsMojoManager();
  CameraDiagnosticsMojoManager(const CameraDiagnosticsMojoManager&) = delete;
  CameraDiagnosticsMojoManager& operator=(const CameraDiagnosticsMojoManager&) =
      delete;
  CameraDiagnosticsMojoManager(CameraDiagnosticsMojoManager&&) = delete;
  CameraDiagnosticsMojoManager& operator=(CameraDiagnosticsMojoManager&&) =
      delete;

  ~CameraDiagnosticsMojoManager() = default;

  scoped_refptr<base::SequencedTaskRunner>& GetTaskRunner();

  // This should only be called in the IPC sequenced task runner.
  chromeos::mojo_service_manager::mojom::ServiceManager*
  GetMojoServiceManager();

  void SetMojoServiceManagerForTest(
      mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceManager>
          service_manager);

 private:
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      mojo_service_manager_;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_MOJO_MANAGER_H_
