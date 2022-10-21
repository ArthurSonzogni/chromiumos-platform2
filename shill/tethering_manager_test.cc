// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/manager.h"
#include "shill/mock_profile.h"
#include "shill/store/fake_store.h"
#include "shill/store/property_store.h"
#include "shill/store/property_store_test.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"

using testing::NiceMock;
using testing::Return;
using testing::Test;

namespace shill {
namespace {
bool GetConfigMAR(const KeyValueStore& caps) {
  return caps.Get<bool>(kTetheringConfMARProperty);
}
bool GetConfigAutoDisable(const KeyValueStore& caps) {
  return caps.Get<bool>(kTetheringConfAutoDisableProperty);
}
std::string GetConfigSSID(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfSSIDProperty);
}
std::string GetConfigPassphrase(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfPassphraseProperty);
}
std::string GetConfigSecurity(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfSecurityProperty);
}
std::string GetConfigBand(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfBandProperty);
}
std::string GetConfigUpstream(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfUpstreamTechProperty);
}
void SetConfigMAR(KeyValueStore& caps, bool value) {
  caps.Set<bool>(kTetheringConfMARProperty, value);
}
void SetConfigAutoDisable(KeyValueStore& caps, bool value) {
  caps.Set<bool>(kTetheringConfAutoDisableProperty, value);
}
void SetConfigSSID(KeyValueStore& caps, const std::string& value) {
  caps.Set<std::string>(kTetheringConfSSIDProperty, value);
}
void SetConfigPassphrase(KeyValueStore& caps, const std::string& value) {
  caps.Set<std::string>(kTetheringConfPassphraseProperty, value);
}
void SetConfigSecurity(KeyValueStore& caps, const std::string& value) {
  caps.Set<std::string>(kTetheringConfSecurityProperty, value);
}
void SetConfigBand(KeyValueStore& caps, const std::string& value) {
  caps.Set<std::string>(kTetheringConfBandProperty, value);
}
void SetConfigUpstream(KeyValueStore& caps, const std::string& value) {
  caps.Set<std::string>(kTetheringConfUpstreamTechProperty, value);
}
}  // namespace

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

  void SetAllowed(TetheringManager* tethering_manager, bool allowed) {
    Error error;
    PropertyStore store;
    tethering_manager->InitPropertyStore(&store);
    store.SetBoolProperty(kTetheringAllowedProperty, allowed, &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  KeyValueStore GetCapabilities(TetheringManager* tethering_manager) {
    Error error;
    KeyValueStore caps = tethering_manager->GetCapabilities(&error);
    EXPECT_TRUE(error.IsSuccess());
    return caps;
  }

  bool SetAndPersistConfig(TetheringManager* tethering_manager,
                           const KeyValueStore& config) {
    Error error;
    bool is_success = tethering_manager->SetAndPersistConfig(config, &error);
    EXPECT_EQ(is_success, error.IsSuccess());
    return is_success;
  }

  bool SetEnabled(TetheringManager* tethering_manager, bool enabled) {
    Error error;
    bool is_success = tethering_manager->SetEnabled(enabled, &error);
    EXPECT_EQ(is_success, error.IsSuccess());
    return is_success;
  }

  KeyValueStore GetConfig(TetheringManager* tethering_manager) {
    Error error;
    KeyValueStore caps = tethering_manager->GetConfig(&error);
    EXPECT_TRUE(error.IsSuccess());
    return caps;
  }

  bool SaveConfig(TetheringManager* tethering_manager,
                  StoreInterface* storage) {
    return tethering_manager->Save(storage);
  }

  bool FromProperties(TetheringManager* tethering_manager,
                      const KeyValueStore& config) {
    return tethering_manager->FromProperties(config);
  }

  KeyValueStore VerifyDefaultTetheringConfig(
      TetheringManager* tethering_manager) {
    KeyValueStore caps = GetConfig(tethering_manager);
    EXPECT_TRUE(GetConfigMAR(caps));
    EXPECT_TRUE(GetConfigAutoDisable(caps));
    std::string ssid = GetConfigSSID(caps);
    EXPECT_FALSE(ssid.empty());
    EXPECT_TRUE(std::all_of(ssid.begin(), ssid.end(), ::isxdigit));
    std::string passphrase = GetConfigPassphrase(caps);
    EXPECT_FALSE(passphrase.empty());
    EXPECT_TRUE(std::all_of(passphrase.begin(), passphrase.end(), ::isxdigit));
    EXPECT_EQ(kSecurityWpa2, GetConfigSecurity(caps));
    EXPECT_FALSE(caps.Contains<std::string>(kTetheringConfBandProperty));
    EXPECT_FALSE(
        caps.Contains<std::string>(kTetheringConfUpstreamTechProperty));
    return caps;
  }

  KeyValueStore GenerateFakeConfig(const std::string& ssid,
                                   const std::string passphrase) {
    KeyValueStore config;
    SetConfigMAR(config, false);
    SetConfigAutoDisable(config, false);
    SetConfigSSID(config, ssid);
    SetConfigPassphrase(config, passphrase);
    SetConfigSecurity(config, kSecurityWpa3);
    SetConfigBand(config, kBand2GHz);
    SetConfigUpstream(config, kTypeCellular);
    return config;
  }

 protected:
  scoped_refptr<MockWiFi> device_;
};

TEST_F(TetheringManagerTest, GetTetheringCapabilities) {
  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager()->RegisterDevice(device_);
  SetAllowed(manager()->tethering_manager(), true);

  KeyValueStore caps = GetCapabilities(manager()->tethering_manager());
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
  SetAllowed(manager.tethering_manager(), true);

  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kDefaultProfile));

  // Check default TetheringConfig.
  VerifyDefaultTetheringConfig(manager.tethering_manager());

  // Fake Tethering configuration.
  std::string ssid = "757365725F73736964";  // "user_ssid" in hex
  std::string passphrase = "user_password";
  KeyValueStore args = GenerateFakeConfig(ssid, passphrase);

  // Block SetAndPersistConfig when no user has logged in.
  EXPECT_FALSE(SetAndPersistConfig(manager.tethering_manager(), args));

  // SetAndPersistConfig succeeds when a user is logged in.
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  EXPECT_TRUE(SetAndPersistConfig(manager.tethering_manager(), args));

  // Read the configuration and check if it matches.
  KeyValueStore config = GetConfig(manager.tethering_manager());
  EXPECT_FALSE(GetConfigMAR(config));
  EXPECT_FALSE(GetConfigAutoDisable(config));
  EXPECT_EQ(GetConfigSSID(config), ssid);
  EXPECT_EQ(GetConfigPassphrase(config), passphrase);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);

  // Log out user and check user's tethering config is not present.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kUserProfile));
  KeyValueStore default_config = GetConfig(manager.tethering_manager());
  EXPECT_NE(GetConfigSSID(default_config), ssid);
  EXPECT_NE(GetConfigPassphrase(default_config), passphrase);

  // Log in user and check tethering config again.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  config = GetConfig(manager.tethering_manager());
  EXPECT_FALSE(GetConfigMAR(config));
  EXPECT_FALSE(GetConfigAutoDisable(config));
  EXPECT_EQ(GetConfigSSID(config), ssid);
  EXPECT_EQ(GetConfigPassphrase(config), passphrase);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);
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
  SetAllowed(manager.tethering_manager(), true);

  // SetEnabled fails for the default profile.
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kDefaultProfile));
  EXPECT_FALSE(SetEnabled(manager.tethering_manager(), true));

  // SetEnabled succeeds for a user profile.
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  KeyValueStore config = GetConfig(manager.tethering_manager());
  EXPECT_TRUE(SetEnabled(manager.tethering_manager(), true));

  // Log out user and check a new SSID and passphrase is generated.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kUserProfile));
  KeyValueStore default_config = GetConfig(manager.tethering_manager());
  EXPECT_NE(GetConfigSSID(config), GetConfigSSID(default_config));
  EXPECT_NE(GetConfigPassphrase(config), GetConfigPassphrase(default_config));

  // Log in user and check the tethering config matches.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));
  KeyValueStore new_config = GetConfig(manager.tethering_manager());
  EXPECT_EQ(GetConfigMAR(config), GetConfigMAR(new_config));
  EXPECT_EQ(GetConfigAutoDisable(config), GetConfigAutoDisable(new_config));
  EXPECT_EQ(GetConfigSSID(config), GetConfigSSID(new_config));
  EXPECT_EQ(GetConfigPassphrase(config), GetConfigPassphrase(new_config));
  EXPECT_FALSE(new_config.Contains<std::string>(kTetheringConfBandProperty));
  EXPECT_FALSE(
      new_config.Contains<std::string>(kTetheringConfUpstreamTechProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigLoadAndUnload) {
  std::string ssid = "757365725F73736964";  // "user_ssid" in hex
  std::string passphrase = "user_password";

  // Check properties of the default tethering configuration.
  VerifyDefaultTetheringConfig(manager()->tethering_manager());

  // Prepare faked tethering configuration stored for a fake user profile.
  FakeStore store;
  store.SetBool(TetheringManager::kStorageId, kTetheringConfAutoDisableProperty,
                true);
  store.SetBool(TetheringManager::kStorageId, kTetheringConfMARProperty, true);
  store.SetString(TetheringManager::kStorageId, kTetheringConfSSIDProperty,
                  ssid);
  store.SetString(TetheringManager::kStorageId,
                  kTetheringConfPassphraseProperty, passphrase);
  store.SetString(TetheringManager::kStorageId, kTetheringConfSecurityProperty,
                  kSecurityWpa3);
  store.SetString(TetheringManager::kStorageId, kTetheringConfBandProperty,
                  kBand5GHz);
  store.SetString(TetheringManager::kStorageId,
                  kTetheringConfUpstreamTechProperty, kTypeCellular);
  scoped_refptr<MockProfile> profile =
      new MockProfile(manager(), "~user/profile0");
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&store));

  // Check faked properties are loaded.
  manager()->tethering_manager()->LoadConfigFromProfile(profile);
  KeyValueStore caps = GetConfig(manager()->tethering_manager());
  EXPECT_TRUE(GetConfigMAR(caps));
  EXPECT_TRUE(GetConfigAutoDisable(caps));
  EXPECT_EQ(ssid, GetConfigSSID(caps));
  EXPECT_EQ(passphrase, GetConfigPassphrase(caps));
  EXPECT_EQ(kSecurityWpa3, GetConfigSecurity(caps));
  EXPECT_EQ(kBand5GHz, GetConfigBand(caps));
  EXPECT_EQ(kTypeCellular, GetConfigUpstream(caps));

  // Check the tethering config is reset to default properties when unloading
  // the profile.
  manager()->tethering_manager()->UnloadConfigFromProfile();
  caps = VerifyDefaultTetheringConfig(manager()->tethering_manager());
  EXPECT_NE(ssid, caps.Get<std::string>(kTetheringConfSSIDProperty));
  EXPECT_NE(passphrase,
            caps.Get<std::string>(kTetheringConfPassphraseProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigSaveAndLoad) {
  // Load a fake tethering configuration.
  KeyValueStore config1 =
      GenerateFakeConfig("757365725F73736964", "user_password");
  FromProperties(manager()->tethering_manager(), config1);

  // Save the fake tethering configuration
  FakeStore store;
  SaveConfig(manager()->tethering_manager(), &store);

  // Force the default configuration to change by unloading the profile.
  manager()->tethering_manager()->UnloadConfigFromProfile();

  // Reload the configuration
  scoped_refptr<MockProfile> profile =
      new MockProfile(manager(), "~user/profile0");
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&store));
  manager()->tethering_manager()->LoadConfigFromProfile(profile);

  // Check that the configurations are identical
  KeyValueStore config2 = GetConfig(manager()->tethering_manager());
  EXPECT_EQ(GetConfigMAR(config1), GetConfigMAR(config2));
  EXPECT_EQ(GetConfigAutoDisable(config1), GetConfigAutoDisable(config2));
  EXPECT_EQ(GetConfigSSID(config1), GetConfigSSID(config2));
  EXPECT_EQ(GetConfigPassphrase(config1), GetConfigPassphrase(config2));
  EXPECT_EQ(GetConfigBand(config1), GetConfigBand(config2));
  EXPECT_EQ(GetConfigUpstream(config1), GetConfigUpstream(config2));
}

TEST_F(TetheringManagerTest, TetheringIsNotAllowed) {
  const char kUserProfile[] = "~user/profile";
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  ON_CALL(*device_, SupportAP()).WillByDefault(Return(true));
  manager.RegisterDevice(device_);

  // Fake Tethering configuration.
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");

  // Push a user profile
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kUserProfile));

  // Tethering is not allowed. SetAndPersistConfig and SetEnabled should fail.
  SetAllowed(manager.tethering_manager(), false);
  EXPECT_FALSE(SetAndPersistConfig(manager.tethering_manager(), config));
  EXPECT_FALSE(SetEnabled(manager.tethering_manager(), true));

  // Tethering is allowed. SetAndPersistConfig and SetEnabled should succeed.
  SetAllowed(manager.tethering_manager(), true);
  EXPECT_TRUE(SetAndPersistConfig(manager.tethering_manager(), config));
  EXPECT_TRUE(SetEnabled(manager.tethering_manager(), true));
}

}  // namespace shill
