// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/manager.h"
#include "shill/store/property_store_test.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"

using testing::NiceMock;
using testing::Return;
using testing::Test;

namespace shill {

// Fake MAC address.
constexpr char kDeviceAddress[] = "aabbccddeeff";

class TetheringManagerTest : public PropertyStoreTest {
 public:
  TetheringManagerTest()
      : device_(new NiceMock<MockWiFi>(
            manager(), "wifi", kDeviceAddress, 0, new MockWakeOnWiFi())) {}
  ~TetheringManagerTest() override = default;

 protected:
  scoped_refptr<MockWiFi> device_;
};

TEST_F(TetheringManagerTest, GetTetheringCapabilities) {
  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager()->RegisterDevice(device_);

  KeyValueStore caps;
  Error error;
  caps = manager()->tethering_manager()->GetCapabilities(&error);
  std::vector<std::string> tech_v;
  tech_v = caps.Get<std::vector<std::string>>(kTetheringCapUpstreamProperty);
  EXPECT_FALSE(tech_v.empty());
  std::set<std::string> tech_s(tech_v.begin(), tech_v.end());
  EXPECT_TRUE(tech_s.count(kTypeEthernet));
#if defined(DISABLE_CELLULAR)
  EXPECT_FALSE(tech_s.count(kTypeCellular));
#else
  EXPECT_TRUE(tech_s.count(kTypeCellular));
#endif  // DISABLE_CELLULAR

  tech_v = caps.Get<std::vector<std::string>>(kTetheringCapDownstreamProperty);
  EXPECT_FALSE(tech_v.empty());
  EXPECT_EQ(tech_v.front(), kTypeWifi);
  std::vector<std::string> wifi_security =
      caps.Get<std::vector<std::string>>(kTetheringCapSecurityProperty);
  EXPECT_FALSE(wifi_security.empty());
}

}  // namespace shill
