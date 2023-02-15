// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd.h"

#include <memory>
#include <utility>

#include <base/threading/thread_task_runner_handle.h>
#include <brillo/udev/udev_monitor.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

namespace diagnostics {

CrosHealthd::CrosHealthd(mojo::PlatformChannelEndpoint endpoint,
                         std::unique_ptr<brillo::UdevMonitor>&& udev_monitor)
    : ipc_support_(base::ThreadTaskRunnerHandle::Get(),
                   mojo::core::ScopedIPCSupport::ShutdownPolicy::
                       CLEAN /* blocking shutdown */),
      context_(std::move(endpoint),
               std::move(udev_monitor),
               base::BindOnce(&CrosHealthd::Quit, base::Unretained(this))) {}

CrosHealthd::~CrosHealthd() = default;

}  // namespace diagnostics
