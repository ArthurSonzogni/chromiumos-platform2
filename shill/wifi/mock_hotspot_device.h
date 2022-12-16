// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_HOTSPOT_DEVICE_H_
#define SHILL_WIFI_MOCK_HOTSPOT_DEVICE_H_

#include "shill/wifi/hotspot_device.h"

#include <gmock/gmock.h>

namespace shill {

class MockHotspotDevice : public HotspotDevice {
 public:
  MockHotspotDevice(Manager* manager,
                    const std::string& link_name,
                    const std::string& mac_address,
                    uint32_t phy_index,
                    const EventCallback& callback)
      : HotspotDevice(manager, link_name, mac_address, phy_index, callback) {}
  ~MockHotspotDevice() override = default;

  bool Start() override { return true; }

  bool Stop() override { return true; }

  MOCK_METHOD(bool,
              ConfigureService,
              (std::unique_ptr<LocalService>),
              (override));
  MOCK_METHOD(bool, DeconfigureService, (), (override));
  MOCK_METHOD(bool, IsServiceUp, (), (const, override));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_HOTSPOT_DEVICE_H_
