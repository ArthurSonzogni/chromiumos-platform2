// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/action_runner.h"

#include <base/check.h>
#include <base/logging.h>
#include <power_manager/dbus-proxies.h>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

ActionRunner::ActionRunner(DbusConnector* dbus_connector)
    : dbus_connector_(dbus_connector) {
  CHECK(dbus_connector_) << "DbusConnector object is nullptr.";
}

ActionRunner::~ActionRunner() = default;

void ActionRunner::Run(mojom::ServiceName name, mojom::ActionType action) {
  switch (action) {
    case mojom::ActionType::kUnmappedEnumField:
      break;
    case mojom::ActionType::kNoOperation:
      break;
    case mojom::ActionType::kNormalReboot:
      if (!allow_normal_reboot_) {
        LOG(WARNING) << "Heartd is not allowed to normal reboot the device.";
        break;
      }

      LOG(WARNING) << "Heartd starts to reboot the device.";
      // There is nothing to do for heartd when it's sucess or error. When
      // failure, power manager should understand why it fails. We just need to
      // check the log.
      dbus_connector_->power_manager_proxy()->RequestRestartAsync(
          /*in_reason = */ 2,
          /*in_description = */ "heartd reset",
          /*success_callback = */ base::DoNothing(),
          /*error_callback = */ base::DoNothing());
      break;
    case mojom::ActionType::kForceReboot:
      if (!allow_force_reboot_) {
        LOG(WARNING) << "Heartd is not allowed to force reboot the device.";
        break;
      }
      break;
  }
}

void ActionRunner::EnableNormalRebootAction() {
  allow_normal_reboot_ = true;
}

void ActionRunner::EnableForceRebootAction() {
  allow_force_reboot_ = true;
}

void ActionRunner::DisableNormalRebootAction() {
  allow_normal_reboot_ = false;
}

void ActionRunner::DisableForceRebootAction() {
  allow_force_reboot_ = false;
}

}  // namespace heartd
