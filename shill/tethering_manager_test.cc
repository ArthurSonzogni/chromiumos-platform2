// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <memory>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/mock_callback.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_cellular_service.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/error.h"
#include "shill/ethernet/mock_ethernet_provider.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/store/fake_store.h"
#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/upstart/mock_upstart.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/local_service.h"
#include "shill/wifi/mock_hotspot_device.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"
#include "shill/wifi/mock_wifi_provider.h"

using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::Test;

namespace shill {
namespace {

// Fake profile identities
constexpr char kDefaultProfile[] = "default";
constexpr char kUserProfile[] = "~user/profile";

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

base::ScopedTempDir MakeTempDir() {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  return temp_dir;
}
}  // namespace

class TetheringManagerTest : public testing::Test {
 public:
  TetheringManagerTest()
      : temp_dir_(MakeTempDir()),
        path_(temp_dir_.GetPath().value()),
        manager_(
            &control_interface_, &dispatcher_, &metrics_, path_, path_, path_),
        modem_info_(&control_interface_, &manager_),
        tethering_manager_(manager_.tethering_manager()),
        wifi_provider_(new NiceMock<MockWiFiProvider>()),
        ethernet_provider_(new NiceMock<MockEthernetProvider>()),
        upstart_(new NiceMock<MockUpstart>(&control_interface_)),
        hotspot_device_(new NiceMock<MockHotspotDevice>(
            &manager_, "wlan0", "ap0", "", 0, event_cb_.Get())) {
    // Replace the manager's WiFi provider with our mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Replace the manager's Ethernet provider with mock.
    manager_.ethernet_provider_.reset(ethernet_provider_);
    // Update the manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    // Replace the manager's upstart instance with mock.
    manager_.upstart_.reset(upstart_);
    ON_CALL(manager_, cellular_service_provider())
        .WillByDefault(Return(&cellular_service_provider_));
    ON_CALL(manager_, modem_info()).WillByDefault(Return(&modem_info_));
  }
  ~TetheringManagerTest() override = default;

  scoped_refptr<MockWiFi> MakeWiFi(const std::string& ifname,
                                   const std::string& mac) {
    return new NiceMock<MockWiFi>(&manager_, ifname, mac, 1, 1,
                                  new MockWakeOnWiFi());
  }

  scoped_refptr<MockCellular> MakeCellular(const std::string& link_name,
                                           const std::string& address,
                                           int interface_index) {
    return new NiceMock<MockCellular>(&manager_, link_name, address,
                                      interface_index, "", RpcIdentifier(""));
  }

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

  void SetEnabled(TetheringManager* tethering_manager, bool enabled) {
    tethering_manager->SetEnabled(enabled, result_cb_.Get());
  }

  void VerifyResult(TetheringManager::SetEnabledResult expected_result) {
    EXPECT_CALL(result_cb_, Run(expected_result));
    DispatchPendingEvents();
    Mock::VerifyAndClearExpectations(&result_cb_);
  }

  void SetEnabledVerifyResult(
      TetheringManager* tethering_manager,
      bool enabled,
      TetheringManager::SetEnabledResult expected_result) {
    SetEnabled(tethering_manager, enabled);
    DownStreamDeviceEvent(tethering_manager_,
                          LocalDevice::DeviceEvent::kServiceUp,
                          hotspot_device_.get());
    VerifyResult(expected_result);
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
    EXPECT_EQ(GetConfigBand(caps), kBandAll);
    EXPECT_TRUE(caps.Contains<std::string>(kTetheringConfUpstreamTechProperty));
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

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

  void TetheringPrerequisite(TetheringManager* tethering_manager) {
    SetAllowed(tethering_manager, true);

    ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
    EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));
    ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
    ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
    EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  }

  void SetDownstream(TetheringManager* tethering_manager) {
    ON_CALL(*wifi_provider_, CreateHotspotDevice(_, _, _, _))
        .WillByDefault(Return(hotspot_device_));
    ON_CALL(*hotspot_device_.get(), ConfigureService(_))
        .WillByDefault(Return(true));
    ON_CALL(*hotspot_device_.get(), IsServiceUp()).WillByDefault(Return(true));
  }

  void DownStreamDeviceEvent(TetheringManager* tethering_manager,
                             LocalDevice::DeviceEvent event,
                             LocalDevice* device) {
    tethering_manager->OnDownstreamDeviceEvent(event, device);
  }

  TetheringManager::TetheringState TetheringState(
      TetheringManager* tethering_manager) {
    return tethering_manager->state_;
  }

  void CheckTetheringIdle(TetheringManager* tethering_manager,
                          const char* reason) {
    EXPECT_EQ(tethering_manager->hotspot_dev_, nullptr);
    EXPECT_EQ(TetheringState(tethering_manager),
              TetheringManager::TetheringState::kTetheringIdle);
    auto status = GetStatus(tethering_manager);
    EXPECT_EQ(status.Get<std::string>(kTetheringStatusIdleReasonProperty),
              reason);
  }

  KeyValueStore GetStatus(TetheringManager* tethering_manager) {
    return tethering_manager->GetStatus();
  }

  const base::CancelableOnceClosure& GetInactiveTimer(
      TetheringManager* tethering_manager) {
    return tethering_manager->inactive_timer_callback_;
  }

  void AddServiceToCellularProvider(CellularServiceRefPtr service) {
    cellular_service_provider_.AddService(service);
  }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      event_cb_;
  StrictMock<base::MockOnceCallback<void(TetheringManager::SetEnabledResult)>>
      result_cb_;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  base::ScopedTempDir temp_dir_;
  std::string path_;
  MockManager manager_;
  MockModemInfo modem_info_;
  TetheringManager* tethering_manager_;
  MockWiFiProvider* wifi_provider_;
  MockEthernetProvider* ethernet_provider_;
  CellularServiceProvider cellular_service_provider_{&manager_};
  MockUpstart* upstart_;
  scoped_refptr<MockHotspotDevice> hotspot_device_;
};

TEST_F(TetheringManagerTest, GetTetheringCapabilities) {
  scoped_refptr<MockWiFi> wifi = MakeWiFi("wlan0", "0a:0b:0c:0d:0e:0f");
  ON_CALL(*wifi, SupportAP()).WillByDefault(Return(true));
  const std::vector<DeviceRefPtr> devices = {wifi};
  ON_CALL(manager_, FilterByTechnology(Technology::kWiFi))
      .WillByDefault(Return(devices));
  SetAllowed(tethering_manager_, true);
  KeyValueStore caps = GetCapabilities(tethering_manager_);

  auto upstream_technologies =
      caps.Get<std::vector<std::string>>(kTetheringCapUpstreamProperty);
  EXPECT_FALSE(upstream_technologies.empty());
  EXPECT_TRUE(base::Contains(upstream_technologies, kTypeEthernet));
  EXPECT_TRUE(base::Contains(upstream_technologies, kTypeCellular));
  EXPECT_FALSE(base::Contains(upstream_technologies, kTypeWifi));

  auto downstream_technologies =
      caps.Get<std::vector<std::string>>(kTetheringCapDownstreamProperty);
  EXPECT_FALSE(downstream_technologies.empty());
  EXPECT_FALSE(base::Contains(downstream_technologies, kTypeEthernet));
  EXPECT_FALSE(base::Contains(downstream_technologies, kTypeCellular));
  EXPECT_TRUE(base::Contains(downstream_technologies, kTypeWifi));

  std::vector<std::string> wifi_security =
      caps.Get<std::vector<std::string>>(kTetheringCapSecurityProperty);
  EXPECT_FALSE(wifi_security.empty());
}

TEST_F(TetheringManagerTest, GetTetheringCapabilitiesWithoutWiFi) {
  const std::vector<DeviceRefPtr> devices;
  ON_CALL(manager_, FilterByTechnology(Technology::kWiFi))
      .WillByDefault(Return(devices));
  SetAllowed(tethering_manager_, true);

  KeyValueStore caps = GetCapabilities(tethering_manager_);

  auto upstream_technologies =
      caps.Get<std::vector<std::string>>(kTetheringCapUpstreamProperty);
  EXPECT_FALSE(upstream_technologies.empty());
  EXPECT_TRUE(base::Contains(upstream_technologies, kTypeEthernet));
  EXPECT_TRUE(base::Contains(upstream_technologies, kTypeCellular));
  EXPECT_FALSE(base::Contains(upstream_technologies, kTypeWifi));

  auto downstream_technologies =
      caps.Get<std::vector<std::string>>(kTetheringCapDownstreamProperty);
  EXPECT_TRUE(downstream_technologies.empty());

  EXPECT_FALSE(
      caps.Contains<std::vector<std::string>>(kTetheringCapSecurityProperty));
}

TEST_F(TetheringManagerTest, TetheringConfig) {
  SetAllowed(tethering_manager_, true);

  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));

  // Check default TetheringConfig.
  VerifyDefaultTetheringConfig(tethering_manager_);

  // Fake Tethering configuration.
  std::string ssid = "757365725F73736964";  // "user_ssid" in hex
  std::string passphrase = "user_password";
  KeyValueStore args = GenerateFakeConfig(ssid, passphrase);

  // Block SetAndPersistConfig when no user has logged in.
  EXPECT_FALSE(SetAndPersistConfig(tethering_manager_, args));

  // SetAndPersistConfig succeeds when a user is logged in.
  ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, args));

  // Read the configuration and check if it matches.
  KeyValueStore config = GetConfig(tethering_manager_);
  EXPECT_FALSE(GetConfigMAR(config));
  EXPECT_FALSE(GetConfigAutoDisable(config));
  EXPECT_EQ(GetConfigSSID(config), ssid);
  EXPECT_EQ(GetConfigPassphrase(config), passphrase);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);

  // Log out user and check user's tethering config is not present.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  KeyValueStore default_config = GetConfig(tethering_manager_);
  EXPECT_NE(GetConfigSSID(default_config), ssid);
  EXPECT_NE(GetConfigPassphrase(default_config), passphrase);

  // Log in user and check tethering config again.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  config = GetConfig(tethering_manager_);
  EXPECT_FALSE(GetConfigMAR(config));
  EXPECT_FALSE(GetConfigAutoDisable(config));
  EXPECT_EQ(GetConfigSSID(config), ssid);
  EXPECT_EQ(GetConfigPassphrase(config), passphrase);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);
}

TEST_F(TetheringManagerTest, DefaultConfigCheck) {
  SetAllowed(tethering_manager_, true);

  // SetEnabled fails for the default profile.
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kNotAllowed);

  // SetEnabled succeeds for a user profile.
  ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  KeyValueStore config = GetConfig(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Log out user and check a new SSID and passphrase is generated.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  KeyValueStore default_config = GetConfig(tethering_manager_);
  EXPECT_NE(GetConfigSSID(config), GetConfigSSID(default_config));
  EXPECT_NE(GetConfigPassphrase(config), GetConfigPassphrase(default_config));
  // Log out user should also stop active tethering session and put tethering
  // state to idle.
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonUserExit);

  // Log in user and check the tethering config matches.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  KeyValueStore new_config = GetConfig(tethering_manager_);
  EXPECT_EQ(GetConfigMAR(config), GetConfigMAR(new_config));
  EXPECT_EQ(GetConfigAutoDisable(config), GetConfigAutoDisable(new_config));
  EXPECT_EQ(GetConfigSSID(config), GetConfigSSID(new_config));
  EXPECT_EQ(GetConfigPassphrase(config), GetConfigPassphrase(new_config));
  EXPECT_EQ(GetConfigBand(config), kBandAll);
  EXPECT_TRUE(
      new_config.Contains<std::string>(kTetheringConfUpstreamTechProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigLoadAndUnload) {
  std::string ssid = "757365725F73736964";  // "user_ssid" in hex
  std::string passphrase = "user_password";

  // Check properties of the default tethering configuration.
  VerifyDefaultTetheringConfig(tethering_manager_);

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
      new MockProfile(&manager_, "~user/profile0");
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&store));

  // Check faked properties are loaded.
  tethering_manager_->LoadConfigFromProfile(profile);
  KeyValueStore caps = GetConfig(tethering_manager_);
  EXPECT_TRUE(GetConfigMAR(caps));
  EXPECT_TRUE(GetConfigAutoDisable(caps));
  EXPECT_EQ(ssid, GetConfigSSID(caps));
  EXPECT_EQ(passphrase, GetConfigPassphrase(caps));
  EXPECT_EQ(kSecurityWpa3, GetConfigSecurity(caps));
  EXPECT_EQ(kBand5GHz, GetConfigBand(caps));
  EXPECT_EQ(kTypeCellular, GetConfigUpstream(caps));

  // Check the tethering config is reset to default properties when unloading
  // the profile.
  tethering_manager_->UnloadConfigFromProfile();
  caps = VerifyDefaultTetheringConfig(tethering_manager_);
  EXPECT_NE(ssid, caps.Get<std::string>(kTetheringConfSSIDProperty));
  EXPECT_NE(passphrase,
            caps.Get<std::string>(kTetheringConfPassphraseProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigSaveAndLoad) {
  // Load a fake tethering configuration.
  KeyValueStore config1 =
      GenerateFakeConfig("757365725F73736964", "user_password");
  FromProperties(tethering_manager_, config1);

  // Save the fake tethering configuration
  FakeStore store;
  SaveConfig(tethering_manager_, &store);

  // Force the default configuration to change by unloading the profile.
  tethering_manager_->UnloadConfigFromProfile();

  // Reload the configuration
  scoped_refptr<MockProfile> profile =
      new MockProfile(&manager_, "~user/profile0");
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&store));
  tethering_manager_->LoadConfigFromProfile(profile);

  // Check that the configurations are identical
  KeyValueStore config2 = GetConfig(tethering_manager_);
  EXPECT_EQ(GetConfigMAR(config1), GetConfigMAR(config2));
  EXPECT_EQ(GetConfigAutoDisable(config1), GetConfigAutoDisable(config2));
  EXPECT_EQ(GetConfigSSID(config1), GetConfigSSID(config2));
  EXPECT_EQ(GetConfigPassphrase(config1), GetConfigPassphrase(config2));
  EXPECT_EQ(GetConfigBand(config1), GetConfigBand(config2));
  EXPECT_EQ(GetConfigUpstream(config1), GetConfigUpstream(config2));
}

TEST_F(TetheringManagerTest, TetheringIsNotAllowed) {
  // Fake Tethering configuration.
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");

  // Push a user profile
  ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));

  // Tethering is not allowed. SetAndPersistConfig and SetEnabled should fail
  // with error code kNotAllowed.
  SetAllowed(tethering_manager_, false);
  EXPECT_FALSE(SetAndPersistConfig(tethering_manager_, config));
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kNotAllowed);

  // Tethering is allowed. SetAndPersistConfig and SetEnabled should success
  SetAllowed(tethering_manager_, true);
  SetDownstream(tethering_manager_);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
}

TEST_F(TetheringManagerTest, CheckReadiness) {
  base::MockOnceCallback<void(TetheringManager::EntitlementStatus)> cb;
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");

  // Not allowed.
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kNotAllowed));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  SetAllowed(tethering_manager_, true);

  // No ethernet Device.
  SetConfigUpstream(config, TechnologyName(Technology::kEthernet));
  EXPECT_TRUE(FromProperties(tethering_manager_, config));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Fake Devices: one Ethernet Device, one Cellular Device.
  auto eth =
      new NiceMock<MockDevice>(&manager_, "eth0", "0a:0b:0c:0d:0e:0f", 1);
  auto cell = MakeCellular("wwan0", "000102030405", 2);
  ON_CALL(*eth, technology()).WillByDefault(Return(Technology::kEthernet));
  const std::vector<DeviceRefPtr> eth_devices = {eth};
  const std::vector<DeviceRefPtr> cell_devices = {cell};
  ON_CALL(manager_, FilterByTechnology(Technology::kEthernet))
      .WillByDefault(Return(eth_devices));
  ON_CALL(manager_, FilterByTechnology(Technology::kCellular))
      .WillByDefault(Return(cell_devices));

  // No Service connected on Ethernet.
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Ethernet Service is not connected.
  auto eth_service(new MockService(&manager_));
  eth->set_selected_service_for_testing(eth_service);
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(false));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Service connected on Ethernet
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(true));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kReady));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Cellular upstream.
  scoped_refptr<MockCellularService> cell_service =
      new MockCellularService(&manager_, cell);
  SetConfigUpstream(config, TechnologyName(Technology::kCellular));
  EXPECT_TRUE(FromProperties(tethering_manager_, config));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  AddServiceToCellularProvider(cell_service);
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kReady));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(TetheringManagerTest, SetEnabledResultName) {
  EXPECT_EQ("success", TetheringManager::SetEnabledResultName(
                           TetheringManager::SetEnabledResult::kSuccess));
  EXPECT_EQ("failure", TetheringManager::SetEnabledResultName(
                           TetheringManager::SetEnabledResult::kFailure));
  EXPECT_EQ("not_allowed",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kNotAllowed));
  EXPECT_EQ("invalid_properties",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kInvalidProperties));
  EXPECT_EQ(
      "upstream_not_available",
      TetheringManager::SetEnabledResultName(
          TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable));
}

TEST_F(TetheringManagerTest, StartTetheringSession) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(_, _, _, _))
      .WillOnce(Return(hotspot_device_));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  SetEnabled(tethering_manager_, true);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // On service up with device event.
  SetDownstream(tethering_manager_);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kServiceUp,
                        hotspot_device_.get());
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
}

TEST_F(TetheringManagerTest, FailToCreateLocalInterface) {
  TetheringPrerequisite(tethering_manager_);
  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(_, _, _, _))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_)).Times(0);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kFailure);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonError);
}

TEST_F(TetheringManagerTest, FailToConfigureService) {
  TetheringPrerequisite(tethering_manager_);
  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(_, _, _, _))
      .WillOnce(Return(hotspot_device_));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_))
      .WillOnce(Return(false));
  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));

  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kFailure);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonError);
}

TEST_F(TetheringManagerTest, StopTetheringSession) {
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));
  EXPECT_CALL(*wifi_provider_, DeleteLocalDevice(_)).Times(1);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  SetEnabledVerifyResult(tethering_manager_, false,
                         TetheringManager::SetEnabledResult::kSuccess);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonClientStop);
}

TEST_F(TetheringManagerTest, DeviceEventInterfaceDisabled) {
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));
  EXPECT_CALL(*wifi_provider_, DeleteLocalDevice(_)).Times(1);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kInterfaceDisabled,
                        hotspot_device_.get());
  DispatchPendingEvents();
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonError);
}

TEST_F(TetheringManagerTest, DeviceEventServiceDown) {
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));
  EXPECT_CALL(*wifi_provider_, DeleteLocalDevice(_)).Times(1);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kServiceDown,
                        hotspot_device_.get());
  DispatchPendingEvents();
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonError);
}

TEST_F(TetheringManagerTest, InterfaceDisabledWhenTetheringIsStarting) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(_, _, _, _))
      .WillOnce(Return(hotspot_device_));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_))
      .WillOnce(Return(true));
  SetEnabled(tethering_manager_, true);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  SetDownstream(tethering_manager_);
  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));
  EXPECT_CALL(*wifi_provider_, DeleteLocalDevice(_)).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kInterfaceDisabled,
                        hotspot_device_.get());
  VerifyResult(TetheringManager::SetEnabledResult::kFailure);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonError);
}

TEST_F(TetheringManagerTest, DeviceEventPeerConnectedDisconnected) {
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kPeerConnected,
                        hotspot_device_.get());

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kPeerDisconnected,
                        hotspot_device_.get());
}

TEST_F(TetheringManagerTest, GetStatus) {
  // Check tethering status when idle.
  auto status = GetStatus(tethering_manager_);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusStateProperty),
            kTetheringStateIdle);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusIdleReasonProperty),
            kTetheringIdleReasonInitialState);
  EXPECT_FALSE(
      status.Contains<std::string>(kTetheringStatusUpstreamTechProperty));
  EXPECT_FALSE(
      status.Contains<std::string>(kTetheringStatusDownstreamTechProperty));
  EXPECT_FALSE(status.Contains<Stringmaps>(kTetheringStatusClientsProperty));

  // Enabled tethering.
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
  status = GetStatus(tethering_manager_);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusStateProperty),
            kTetheringStateActive);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusUpstreamTechProperty),
            kTypeCellular);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusDownstreamTechProperty),
            kTypeWifi);
  EXPECT_EQ(status.Get<Stringmaps>(kTetheringStatusClientsProperty).size(), 0);
  EXPECT_FALSE(
      status.Contains<std::string>(kTetheringStatusIdleReasonProperty));

  // Connect 2 clients.
  std::vector<std::vector<uint8_t>> clients;
  clients.push_back({00, 11, 22, 33, 44, 55});
  clients.push_back({00, 11, 22, 33, 44, 66});
  EXPECT_CALL(*hotspot_device_.get(), GetStations()).WillOnce(Return(clients));
  status = GetStatus(tethering_manager_);
  EXPECT_EQ(status.Get<Stringmaps>(kTetheringStatusClientsProperty).size(), 2);

  // Stop tethering.
  ON_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillByDefault(Return(true));
  SetEnabledVerifyResult(tethering_manager_, false,
                         TetheringManager::SetEnabledResult::kSuccess);
  status = GetStatus(tethering_manager_);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusStateProperty),
            kTetheringStateIdle);
  EXPECT_EQ(status.Get<std::string>(kTetheringStatusIdleReasonProperty),
            kTetheringIdleReasonClientStop);
  EXPECT_FALSE(
      status.Contains<std::string>(kTetheringStatusUpstreamTechProperty));
  EXPECT_FALSE(
      status.Contains<std::string>(kTetheringStatusDownstreamTechProperty));
  EXPECT_FALSE(status.Contains<Stringmaps>(kTetheringStatusClientsProperty));
}

TEST_F(TetheringManagerTest, InactiveTimer) {
  // Start tethering.
  TetheringPrerequisite(tethering_manager_);
  SetDownstream(tethering_manager_);
  // Inactive timer is not triggered when tethering is not active.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
  // Inactive timer should be armed when tethering is active and no client is
  // connected.
  EXPECT_FALSE(GetInactiveTimer(tethering_manager_).IsCancelled());

  // Connect client to the hotspot.
  std::vector<std::vector<uint8_t>> clients;
  clients.push_back({00, 11, 22, 33, 44, 55});
  EXPECT_CALL(*hotspot_device_.get(), GetStations()).WillOnce(Return(clients));
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kPeerConnected,
                        hotspot_device_.get());
  DispatchPendingEvents();
  // Inactive timer should be canceled if at least one client is connected.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());

  clients.clear();
  EXPECT_CALL(*hotspot_device_.get(), GetStations()).WillOnce(Return(clients));
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kPeerDisconnected,
                        hotspot_device_.get());
  DispatchPendingEvents();
  // Inactive timer should be re-armed when tethering is active and the last
  // client is gone.
  EXPECT_FALSE(GetInactiveTimer(tethering_manager_).IsCancelled());
}

}  // namespace shill
