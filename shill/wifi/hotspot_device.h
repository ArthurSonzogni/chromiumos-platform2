// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_HOTSPOT_DEVICE_H_
#define SHILL_WIFI_HOTSPOT_DEVICE_H_

#include "shill/wifi/local_device.h"

namespace shill {

class HotspotDevice : public LocalDevice {
 public:
  // Constructor function
  HotspotDevice(Manager* manager,
                const std::string& link_name,
                const std::string& mac_address,
                uint32_t phy_index,
                LocalDevice::EventCallback callback);

  HotspotDevice(const HotspotDevice&) = delete;
  HotspotDevice& operator=(const HotspotDevice&) = delete;

  ~HotspotDevice() override;

  // HotspotDevice start routine. Like connect to wpa_supplicant, register
  // netlink events, clean up any wpa_supplicant networks, etc.
  bool Start() override;

  // HotspotDevice stop routine. Like clean up wpa_supplicant networks,
  // disconnect to wpa_supplicant, deregister netlink events, etc.
  bool Stop() override;
};

}  // namespace shill

#endif  // SHILL_WIFI_HOTSPOT_DEVICE_H_
