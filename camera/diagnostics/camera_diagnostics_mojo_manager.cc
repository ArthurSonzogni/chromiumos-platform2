// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/camera_diagnostics_mojo_manager.h"

#include <memory>
#include <utility>

#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo_service_manager/lib/connect.h>

#include "cros-camera/common.h"

namespace cros {

CameraDiagnosticsMojoManager::CameraDiagnosticsMojoManager() {
  VLOGF(1) << "Initialize mojo IPC";
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);
  ipc_task_runner_ = base::SequencedTaskRunner::GetCurrentDefault();
}

scoped_refptr<base::SequencedTaskRunner>&
CameraDiagnosticsMojoManager::GetTaskRunner() {
  return ipc_task_runner_;
}

chromeos::mojo_service_manager::mojom::ServiceManager*
CameraDiagnosticsMojoManager::GetMojoServiceManager() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  if (!mojo_service_manager_.is_bound() ||
      !mojo_service_manager_.is_connected()) {
    mojo_service_manager_.reset();
    VLOGF(1) << "Mojo service manager is not connected! Connecting...";
    auto pending_remote =
        chromeos::mojo_service_manager::ConnectToMojoServiceManager();
    if (!pending_remote) {
      LOGF(ERROR) << "Failed to connect to mojo service manager!";
    }
    mojo_service_manager_.Bind(std::move(pending_remote));
  }
  return mojo_service_manager_.get();
}

void CameraDiagnosticsMojoManager::SetMojoServiceManagerForTest(
    mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceManager>
        service_manager) {
  LOGF(INFO) << "Set mojo service manager for test";
  mojo_service_manager_.Bind(std::move(service_manager));
}

}  // namespace cros
