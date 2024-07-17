// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_WIFI_PHY_H_
#define SHILL_WIFI_MOCK_WIFI_PHY_H_

#include <set>

#include "shill/wifi/wifi_phy.h"

#include <gmock/gmock.h>

namespace shill {

class MockWiFiPhy : public WiFiPhy {
 public:
  explicit MockWiFiPhy(uint32_t phy_index);

  ~MockWiFiPhy() override;

  void SetFrequencies(const Frequencies& freqs) { frequencies_ = freqs; }

  MOCK_METHOD(void, PhyDumpComplete, (), (override));
  MOCK_METHOD(void, OnNewWiphy, (const Nl80211Message&), (override));
  MOCK_METHOD(bool, SupportAPMode, (), (const, override));
  MOCK_METHOD(bool, SupportAPSTAConcurrency, (), (const, override));
  MOCK_METHOD(bool, SupportP2PMode, (), (const, override));
  MOCK_METHOD(uint32_t,
              SupportsConcurrency,
              (const std::multiset<nl80211_iftype>& iface_types),
              (const, override));
  MOCK_METHOD(bool, reg_self_managed, (), (const, override));
  MOCK_METHOD(std::optional<std::multiset<nl80211_iftype>>,
              RequestNewIface,
              (nl80211_iftype desired_type, Priority priority),
              (const, override));
  MOCK_METHOD(std::vector<int>, GetFrequencies, (), (const, override));
  MOCK_METHOD(std::vector<int>, GetActiveFrequencies, (), (const, override));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_WIFI_PHY_H_
