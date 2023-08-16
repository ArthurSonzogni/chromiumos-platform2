// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_P2P_DEVICE_H_
#define SHILL_WIFI_MOCK_P2P_DEVICE_H_

#include "shill/wifi/p2p_device.h"

#include <string>

#include <gmock/gmock.h>

namespace shill {

class MockP2PDevice : public P2PDevice {
 public:
  MockP2PDevice(Manager* manager,
                LocalDevice::IfaceType iface_type,
                const std::string& primary_link_name,
                uint32_t phy_index,
                uint32_t shill_id,
                const EventCallback& callback)
      : P2PDevice(manager,
                  iface_type,
                  primary_link_name,
                  phy_index,
                  shill_id,
                  callback) {}

  ~MockP2PDevice() = default;

  bool Start() override { return true; }

  bool Stop() override { return true; }
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_P2P_DEVICE_H_
