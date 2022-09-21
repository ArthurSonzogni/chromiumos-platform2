// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
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

  Error::Type TestCreateProfile(Manager* manager, const std::string& name) {
    Error error;
    std::string path;
    manager->CreateProfile(name, &path, &error);
    return error.type();
  }

  Error::Type TestPushProfile(Manager* manager, const std::string& name) {
    Error error;
    std::string path;
    manager->PushProfile(name, &path, &error);
    return error.type();
  }

  Error::Type TestPopProfile(Manager* manager, const std::string& name) {
    Error error;
    manager->PopProfile(name, &error);
    return error.type();
  }

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
  EXPECT_TRUE(tech_s.count(kTypeCellular));

  tech_v = caps.Get<std::vector<std::string>>(kTetheringCapDownstreamProperty);
  EXPECT_FALSE(tech_v.empty());
  EXPECT_EQ(tech_v.front(), kTypeWifi);
  std::vector<std::string> wifi_security =
      caps.Get<std::vector<std::string>>(kTetheringCapSecurityProperty);
  EXPECT_FALSE(wifi_security.empty());
}

TEST_F(TetheringManagerTest, TetheringConfig) {
  const char kDefaultProfile[] = "default";
  const char kUserProfile[] = "~user/profile";

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager.RegisterDevice(device_);
  manager.tethering_manager()->allowed_ = true;

  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kDefaultProfile));

  // Check default TetheringConfig.
  KeyValueStore caps;
  {
    Error error;
    caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
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
  EXPECT_EQ(security, kSecurityWpa2);
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

  // Block SetConfig when no user has logged in.
  {
    Error error;
    EXPECT_FALSE(
        manager.tethering_manager()->SetAndPersistConfig(args, &error));
    EXPECT_FALSE(error.IsSuccess());
  }

  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  {
    Error error;
    EXPECT_TRUE(manager.tethering_manager()->SetAndPersistConfig(args, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // Read and check if match.
  {
    Error error;
    caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
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

  // Log out user and check user's tethering config is not present.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kUserProfile));
  {
    Error error;
    caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
  EXPECT_NE(caps.Get<std::string>(kTetheringConfSSIDProperty), ssid);
  EXPECT_NE(caps.Get<std::string>(kTetheringConfPassphraseProperty),
            passphrase);

  // Log in user and check tethering config again.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  {
    Error error;
    caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
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

TEST_F(TetheringManagerTest, SetTetheringEnabled) {
  const char kDefaultProfile[] = "default";
  const char kUserProfile[] = "~user/profile";

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager.RegisterDevice(device_);
  manager.tethering_manager()->allowed_ = true;

  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kDefaultProfile));

  {
    Error error;
    EXPECT_FALSE(manager.tethering_manager()->SetEnabled(true, &error));
    EXPECT_FALSE(error.IsSuccess());
  }

  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));

  KeyValueStore caps;
  {
    Error error;
    caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }

  {
    Error error;
    EXPECT_TRUE(manager.tethering_manager()->SetEnabled(true, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // Log out user and check a new SSID and passphrase is generated.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kUserProfile));
  KeyValueStore new_caps;
  {
    Error error;
    new_caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
  EXPECT_NE(new_caps.Get<std::string>(kTetheringConfSSIDProperty),
            caps.Get<std::string>(kTetheringConfSSIDProperty));
  EXPECT_NE(new_caps.Get<std::string>(kTetheringConfPassphraseProperty),
            caps.Get<std::string>(kTetheringConfPassphraseProperty));

  // Log in user and check tethering config again.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  {
    Error error;
    new_caps = manager.tethering_manager()->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
  }
  EXPECT_EQ(new_caps.Get<std::string>(kTetheringConfSSIDProperty),
            caps.Get<std::string>(kTetheringConfSSIDProperty));
  EXPECT_EQ(new_caps.Get<std::string>(kTetheringConfPassphraseProperty),
            caps.Get<std::string>(kTetheringConfPassphraseProperty));
}

}  // namespace shill
