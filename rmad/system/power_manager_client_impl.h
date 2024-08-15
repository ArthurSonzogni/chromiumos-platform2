// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_POWER_MANAGER_CLIENT_IMPL_H_
#define RMAD_SYSTEM_POWER_MANAGER_CLIENT_IMPL_H_

#include <memory>

#include <power_manager-client/power_manager/dbus-proxies.h>

#include "rmad/system/power_manager_client.h"

namespace rmad {

class PowerManagerClientImpl : public PowerManagerClient {
 public:
  PowerManagerClientImpl();
  explicit PowerManagerClientImpl(
      std::unique_ptr<org::chromium::PowerManagerProxyInterface>
          power_manager_proxy);
  PowerManagerClientImpl(const PowerManagerClientImpl&) = delete;
  PowerManagerClientImpl& operator=(const PowerManagerClientImpl&) = delete;

  ~PowerManagerClientImpl() override = default;

  bool Restart() override;
  bool Shutdown() override;

 private:
  std::unique_ptr<org::chromium::PowerManagerProxyInterface>
      power_manager_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_POWER_MANAGER_CLIENT_IMPL_H_
