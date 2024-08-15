// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/power_manager_client_impl.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <dbus/power_manager/dbus-constants.h>
#include <power_manager-client/power_manager/dbus-proxies.h>

#include "rmad/utils/dbus_utils.h"

namespace rmad {

PowerManagerClientImpl::PowerManagerClientImpl() {
  power_manager_proxy_ = std::make_unique<org::chromium::PowerManagerProxy>(
      DBus::GetInstance()->bus());
}

PowerManagerClientImpl::PowerManagerClientImpl(
    std::unique_ptr<org::chromium::PowerManagerProxyInterface>
        power_manager_proxy)
    : power_manager_proxy_(std::move(power_manager_proxy)) {}

bool PowerManagerClientImpl::Restart() {
  brillo::ErrorPtr error;
  if (!power_manager_proxy_->RequestRestart(
          /*in_reason=*/power_manager::RequestRestartReason::
              REQUEST_RESTART_OTHER,
          /*in_description=*/"rmad request restart",
          /*error=*/&error) ||
      error) {
    LOG(ERROR) << "Failed to call RequestRestartReason from powerd service";
    return false;
  }

  // There is no reply. Assume success if there's no errors.
  return true;
}

bool PowerManagerClientImpl::Shutdown() {
  brillo::ErrorPtr error;
  if (!power_manager_proxy_->RequestShutdown(
          /*in_reason=*/power_manager::RequestShutdownReason::
              REQUEST_SHUTDOWN_OTHER,
          /*in_description=*/"rmad request shutdown",
          /*error=*/&error) ||
      error) {
    LOG(ERROR) << "Failed to call RequestShutdown from powerd service";
    return false;
  }

  // There is no reply. Assume success if there's no errors.
  return true;
}

}  // namespace rmad
