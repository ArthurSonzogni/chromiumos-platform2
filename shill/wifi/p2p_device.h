// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_DEVICE_H_
#define SHILL_WIFI_P2P_DEVICE_H_

#include <string>

#include "shill/wifi/local_device.h"

namespace shill {

class Manager;

class P2PDevice : public LocalDevice {
 public:
  // Constructor function
  P2PDevice(Manager* manager,
            LocalDevice::IfaceType iface_type,
            const std::string& primary_link_name,
            uint32_t phy_index,
            LocalDevice::EventCallback callback);

  P2PDevice(const P2PDevice&) = delete;
  P2PDevice& operator=(const P2PDevice&) = delete;

  ~P2PDevice() override;

  // P2PDevice start routine. Override the base class Start.
  bool Start() override;

  // P2PDevice stop routine. Override the base class Stop.
  bool Stop() override;

  // Stubbed to return null.
  LocalService* GetService() const override { return nullptr; }

 private:
  friend class P2PDeviceTest;

  // Primary interface link name.
  std::string primary_link_name_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_DEVICE_H_
