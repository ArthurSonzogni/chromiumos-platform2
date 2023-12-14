// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/action_runner.h"

#include <base/logging.h>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

ActionRunner::ActionRunner() = default;

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
