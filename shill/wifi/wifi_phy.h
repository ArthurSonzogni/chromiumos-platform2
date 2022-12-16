// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_PHY_H_
#define SHILL_WIFI_WIFI_PHY_H_

#include <set>
#include <vector>

#include "shill/mockable.h"
#include "shill/net/nl80211_message.h"
#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_provider.h"

namespace shill {

struct IfaceLimit {
  std::vector<nl80211_iftype> iftypes;
  uint32_t max;
};

struct ConcurrencyCombination {
  std::vector<IfaceLimit> limits;
  uint32_t max_num;
  uint32_t num_channels;
};

// A WiFiPhy object represents a wireless physical layer device. Objects of this
// class map 1:1 with an NL80211 "wiphy". WiFiPhy objects are created and owned
// by the WiFiProvider singleton. The lifecycle of a WiFiPhy object begins with
// the netlink command NL80211_CMD_NEW_WIPHY and ends with
// NL80211_CMD_DEL_WIPHY.

// TODO(b/244630773): Update WiFiPhy to store phy cabilities, and update the
// documentation accordingly.

class WiFiPhy {
 public:
  explicit WiFiPhy(uint32_t phy_index);

  virtual ~WiFiPhy();

  // Return the phy index.
  uint32_t GetPhyIndex() const { return phy_index_; }

  // Remove a WiFi device instance from wifi_devices_.
  void DeleteWiFiDevice(WiFiConstRefPtr device);

  // Add a WiFi device instance to wifi_devices_.
  void AddWiFiDevice(WiFiConstRefPtr device);

  // Remove a WiFi local device instance from wifi_local_devices_.
  void DeleteWiFiLocalDevice(LocalDeviceConstRefPtr device);

  // Add a WiFi local device instance to wifi_local_devices_.
  void AddWiFiLocalDevice(LocalDeviceConstRefPtr device);

  // Parse an NL80211_CMD_NEW_WIPHY netlink message.
  // TODO(b/248103586): Move NL80211_CMD_NEW_WIPHY parsing out of WiFiPhy and
  // into WiFiProvider.
  mockable void OnNewWiphy(const Nl80211Message& nl80211_message);

  // Return true if the phy supports iftype, false otherwise.
  bool SupportsIftype(nl80211_iftype iftype);

  std::vector<ConcurrencyCombination> ConcurrencyCombinations() {
    return concurrency_combs_;
  }

 private:
  friend class WiFiPhyTest;
  uint32_t phy_index_;
  std::set<WiFiConstRefPtr> wifi_devices_;
  std::set<LocalDeviceConstRefPtr> wifi_local_devices_;
  std::set<nl80211_iftype> supported_ifaces_;
  void ParseInterfaceTypes(const Nl80211Message& nl80211_message);

  // Parse the NL80211_ATTR_INTERFACE_COMBINATIONS from a new wiphy message and
  // store the contents in concurrency_combs_.
  void ParseConcurrency(const Nl80211Message& nl80211_message);
  std::vector<ConcurrencyCombination> concurrency_combs_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_PHY_H_
