// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_external_task.h"

#include <utility>

#include <chromeos/net-base/process_manager.h>

namespace shill {

MockExternalTask::MockExternalTask(
    ControlInterface* control,
    net_base::ProcessManager* process_manager,
    const base::WeakPtr<RpcTaskDelegate>& task_delegate,
    base::OnceCallback<void(pid_t, int)> death_callback)
    : ExternalTask(
          control, process_manager, task_delegate, std::move(death_callback)) {}

MockExternalTask::~MockExternalTask() = default;

}  // namespace shill
