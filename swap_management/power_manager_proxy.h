// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_POWER_MANAGER_PROXY_H_
#define SWAP_MANAGEMENT_POWER_MANAGER_PROXY_H_

#include <memory>

#include <absl/status/statusor.h>
#include <power_manager/dbus-proxies.h>

#include "swap_management/utils.h"

namespace swap_management {

class PowerManagerProxy {
 public:
  PowerManagerProxy& operator=(const PowerManagerProxy&) = delete;
  PowerManagerProxy(const PowerManagerProxy&) = delete;

  static PowerManagerProxy* Get();
  PowerManagerProxy();
  absl::StatusOr<bool> IsACConnected();

  void RegisterSuspendSignal();

 private:
  friend PowerManagerProxy** GetSingleton<PowerManagerProxy>();

  std::unique_ptr<org::chromium::PowerManagerProxyInterface>
      power_manager_proxy_;
};
}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_POWER_MANAGER_PROXY_H_
