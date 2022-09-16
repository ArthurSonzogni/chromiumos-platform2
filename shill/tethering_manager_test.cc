// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <set>
#include <string>
#include <vector>

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
            manager(), "wifi", kDeviceAddress, 0, 0, new MockWakeOnWiFi())) {}
  ~TetheringManagerTest() override = default;

 protected:
  scoped_refptr<MockWiFi> device_;
};

TEST_F(TetheringManagerTest, GetTetheringCapabilities) {
  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager()->RegisterDevice(device_);
  manager()->tethering_manager()->allowed_ = true;

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

TEST_F(TetheringManagerTest, TetheringConfig) {
  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager()->RegisterDevice(device_);
  manager()->tethering_manager()->allowed_ = true;

  // Check default TetheringConfig.
  KeyValueStore caps;
  Error error;
  caps = manager()->tethering_manager()->GetConfig(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(caps.Get<bool>(kTetheringConfMARProperty));
  EXPECT_TRUE(caps.Get<bool>(kTetheringConfAutoDisableProperty));
  std::string ssid = caps.Get<std::string>(kTetheringConfSSIDProperty);
  EXPECT_FALSE(ssid.empty());
  EXPECT_TRUE(std::all_of(ssid.begin(), ssid.end(), ::isxdigit));
  std::string passphrase =
      caps.Get<std::string>(kTetheringConfPassphraseProperty);
  EXPECT_FALSE(passphrase.empty());
  EXPECT_TRUE(std::all_of(passphrase.begin(), passphrase.end(), ::isxdigit));
  std::string security = caps.Get<std::string>(kTetheringConfSecurityProperty);
  EXPECT_EQ(security, kSecurityWpa2Wpa3);
  EXPECT_FALSE(caps.Contains<std::string>(kTetheringConfBandProperty));
  EXPECT_FALSE(caps.Contains<std::string>(kTetheringConfUpstreamTechProperty));

  // Set TetheringConfig.
  KeyValueStore args;
  args.Set<bool>(kTetheringConfMARProperty, false);
  args.Set<bool>(kTetheringConfAutoDisableProperty, false);
  ssid = "6368726F6D654F532D31323334";
  args.Set<std::string>(kTetheringConfSSIDProperty, ssid);
  passphrase = "test0000";
  args.Set<std::string>(kTetheringConfPassphraseProperty, passphrase);
  args.Set<std::string>(kTetheringConfSecurityProperty, kSecurityWpa3);
  args.Set<std::string>(kTetheringConfBandProperty, kBand2GHz);
  args.Set<std::string>(kTetheringConfUpstreamTechProperty, kTypeCellular);
  EXPECT_TRUE(manager()->tethering_manager()->SetConfig(args, &error));
  EXPECT_TRUE(error.IsSuccess());

  // Read and check if match.
  caps = manager()->tethering_manager()->GetConfig(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(caps.Get<bool>(kTetheringConfMARProperty));
  EXPECT_FALSE(caps.Get<bool>(kTetheringConfAutoDisableProperty));
  EXPECT_EQ(caps.Get<std::string>(kTetheringConfSSIDProperty), ssid);
  EXPECT_EQ(caps.Get<std::string>(kTetheringConfPassphraseProperty),
            passphrase);
  EXPECT_EQ(caps.Get<std::string>(kTetheringConfSecurityProperty),
            kSecurityWpa3);
  EXPECT_EQ(caps.Get<std::string>(kTetheringConfBandProperty), kBand2GHz);
  EXPECT_EQ(caps.Get<std::string>(kTetheringConfUpstreamTechProperty),
            kTypeCellular);
}

}  // namespace shill
