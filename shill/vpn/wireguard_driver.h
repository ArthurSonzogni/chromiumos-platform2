// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_WIREGUARD_DRIVER_H_
#define SHILL_VPN_WIREGUARD_DRIVER_H_

#include <string>

#include "shill/ipconfig.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class WireguardDriver : public VPNDriver {
 public:
  WireguardDriver(Manager* manager, ProcessManager* process_manager);
  WireguardDriver(const WireguardDriver&) = delete;
  WireguardDriver& operator=(const WireguardDriver&) = delete;

  ~WireguardDriver() = default;

  // Inherited from VPNDriver.
  base::TimeDelta ConnectAsync(EventHandler* event_handler) override;
  void Disconnect() override;
  void OnConnectTimeout() override;
  IPConfig::Properties GetIPProperties() const override;
  std::string GetProviderType() const override;

 private:
  static const VPNDriver::Property kProperties[];
};

}  // namespace shill

#endif  // SHILL_VPN_WIREGUARD_DRIVER_H_
