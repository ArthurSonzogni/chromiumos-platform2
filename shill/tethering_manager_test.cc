// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tethering_manager.h"

#include <sys/socket.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/mock_callback.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_cellular_service.h"
#include "shill/cellular/mock_cellular_service_provider.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/error.h"
#include "shill/ethernet/mock_ethernet_provider.h"
#include "shill/http_request.h"
#include "shill/mac_address.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/network/mock_network.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/store/fake_store.h"
#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/upstart/mock_upstart.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/local_service.h"
#include "shill/wifi/mock_hotspot_device.h"
#include "shill/wifi/mock_wifi.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"

using testing::_;
using testing::DoAll;
using testing::DoDefault;
using testing::Eq;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::Unused;
using testing::WithArg;

namespace shill {
namespace {

// Fake profile identities
constexpr char kDefaultProfile[] = "default";
constexpr char kUserProfile[] = "~user/profile";
constexpr uint32_t kPhyIndex = 5678;
constexpr int kTestInterfaceIndex = 3;
constexpr char kTestInterfaceName[] = "wwan0";
constexpr char kTestDownstreamDeviceForTest[] = "wlan5";
constexpr uint32_t kTestDownstreamPhyIndexForTest = 5;
constexpr int kTetheredNetworkId = 411;
constexpr WiFiPhy::Priority kPriorityForTest = WiFiPhy::Priority(4);

// The value below is "testAP-0000" in hex;
constexpr char kTestAPHexSSID[] = "7465737441502d30303030";
constexpr char kTestPassword[] = "user_password";

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
std::string GetConfigDownstreamDeviceForTest(const KeyValueStore& caps) {
  return caps.Get<std::string>(kTetheringConfDownstreamDeviceForTestProperty);
}
uint32_t GetConfigDownstreamPhyIndexForTest(const KeyValueStore& caps) {
  return caps.Get<uint32_t>(kTetheringConfDownstreamPhyIndexForTestProperty);
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
void SetConfigDownstreamDeviceForTest(KeyValueStore& caps,
                                      const std::string& value) {
  caps.Set<std::string>(kTetheringConfDownstreamDeviceForTestProperty, value);
}
void SetConfigDownstreamPhyIndexForTest(KeyValueStore& caps, uint32_t value) {
  caps.Set<uint32_t>(kTetheringConfDownstreamPhyIndexForTestProperty, value);
}

base::ScopedTempDir MakeTempDir() {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  return temp_dir;
}

class MockPatchpanelClient : public patchpanel::FakeClient {
 public:
  MockPatchpanelClient() = default;
  ~MockPatchpanelClient() override = default;

  MOCK_METHOD(bool,
              CreateTetheredNetwork,
              (const std::string&,
               const std::string&,
               const std::optional<DHCPOptions>&,
               const std::optional<UplinkIPv6Configuration>&,
               const std::optional<int>& mtu,
               patchpanel::Client::CreateTetheredNetworkCallback),
              (override));
};

base::ScopedFD MakeFd() {
  return base::ScopedFD(socket(AF_INET, SOCK_DGRAM, 0));
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
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        ethernet_provider_(new NiceMock<MockEthernetProvider>()),
        cellular_service_provider_(
            new NiceMock<MockCellularServiceProvider>(&manager_)),
        upstart_(new NiceMock<MockUpstart>(&control_interface_)),
        hotspot_device_(new NiceMock<MockHotspotDevice>(
            &manager_,
            "wlan0",
            "ap0",
            net_base::MacAddress(0x00, 0x01, 0x02, 0x03, 0x04, 0x05),
            0,
            WiFiPhy::Priority(0),
            event_cb_.Get())),
        network_(new MockNetwork(
            kTestInterfaceIndex, kTestInterfaceName, Technology::kCellular)),
        service_(new MockService(&manager_)),
        wifi_phy_(hotspot_device_->phy_index()) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Replace the Manager's Ethernet provider with a mock.
    manager_.ethernet_provider_.reset(ethernet_provider_);
    // Replace the Manager's Cellular provider with a mock.
    manager_.cellular_service_provider_.reset(cellular_service_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    // Replace the Manager's upstart instance with a mock.
    manager_.upstart_.reset(upstart_);
    // Replace the Manager's patchpanel DBus client with a mock.
    auto patchpanel = std::make_unique<MockPatchpanelClient>();
    patchpanel_ = patchpanel.get();
    manager_.set_patchpanel_client_for_testing(std::move(patchpanel));

    ON_CALL(manager_, cellular_service_provider())
        .WillByDefault(Return(cellular_service_provider_));
    cellular_profile_ = new NiceMock<MockProfile>(&manager_);
    cellular_service_provider_->set_profile_for_testing(cellular_profile_);
    ON_CALL(manager_, modem_info()).WillByDefault(Return(&modem_info_));
    ON_CALL(*wifi_provider_, RequestLocalDeviceCreation)
        .WillByDefault(DoAll(
            InvokeWithoutArgs(this, &TetheringManagerTest::OnDeviceCreated),
            Return(true)));
    ON_CALL(*hotspot_device_.get(), ConfigureService(_))
        .WillByDefault(Return(true));
    ON_CALL(*hotspot_device_.get(), DeconfigureService())
        .WillByDefault(Return(true));
    ON_CALL(*hotspot_device_.get(), IsServiceUp()).WillByDefault(Return(true));
    ON_CALL(*cellular_service_provider_, AcquireTetheringNetwork(_, _, _, _))
        .WillByDefault(Return());
    ON_CALL(*cellular_service_provider_, ReleaseTetheringNetwork(_, _))
        .WillByDefault(Return());
    ON_CALL(*network_, IsConnected()).WillByDefault(Return(true));
    ON_CALL(*wifi_provider_, GetPhyAtIndex(hotspot_device_->phy_index()))
        .WillByDefault(Return(&wifi_phy_));
    wifi_phy_.SetFrequencies(
        {{0, {{.value = 2412}, {.value = 2432}, {.value = 2437}}},
         {1, {{.value = 5220}, {.value = 5240}}}});
  }
  ~TetheringManagerTest() override = default;

  scoped_refptr<MockCellular> MakeCellular(const std::string& link_name,
                                           net_base::MacAddress mac_address,
                                           int interface_index) {
    return new NiceMock<MockCellular>(&manager_, link_name, mac_address,
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

  void Enable(TetheringManager* tethering_manager, WiFiPhy::Priority priority) {
    tethering_manager_->Enable(priority, result_cb_.Get());
  }

  void Disable(TetheringManager* tethering_manager) {
    tethering_manager_->Disable(result_cb_.Get());
  }

  void VerifyResult(TetheringManager::SetEnabledResult expected_result) {
    EXPECT_CALL(result_cb_, Run(expected_result));
    DispatchPendingEvents();
    Mock::VerifyAndClearExpectations(&result_cb_);
    EXPECT_TRUE(GetStartTimer(tethering_manager_).IsCancelled());
  }

  void SetEnabledVerifyResult(
      TetheringManager* tethering_manager,
      bool enabled,
      TetheringManager::SetEnabledResult expected_result) {
    if (enabled) {
      Enable(tethering_manager, kPriorityForTest);
      ON_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
          .WillByDefault(Return(true));
      // Send upstream downstream ready events.
      DownStreamDeviceEvent(tethering_manager,
                            LocalDevice::DeviceEvent::kInterfaceEnabled,
                            hotspot_device_.get());
      DownStreamDeviceEvent(tethering_manager,
                            LocalDevice::DeviceEvent::kLinkUp,
                            hotspot_device_.get());
      OnUpstreamNetworkAcquired(tethering_manager_,
                                TetheringManager::SetEnabledResult::kSuccess);
      OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                               {.network_id = kTetheredNetworkId});
    } else {
      Disable(tethering_manager);
      // Send upstream tear down event
      OnUpstreamNetworkReleased(tethering_manager_, true);
    }
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
    return tethering_manager->FromProperties(config).has_value();
  }

  KeyValueStore VerifyDefaultTetheringConfig(
      TetheringManager* tethering_manager) {
    KeyValueStore caps = GetConfig(tethering_manager);
    EXPECT_TRUE(GetConfigMAR(caps));
    EXPECT_TRUE(tethering_manager->stable_mac_addr_.address().has_value());
    EXPECT_TRUE(GetConfigAutoDisable(caps));
    EXPECT_FALSE(tethering_manager_->experimental_tethering_functionality_);
    std::string ssid = GetConfigSSID(caps);
    EXPECT_FALSE(ssid.empty());
    EXPECT_TRUE(std::all_of(ssid.begin(), ssid.end(), ::isxdigit));
    std::string passphrase = GetConfigPassphrase(caps);
    EXPECT_FALSE(passphrase.empty());
    EXPECT_TRUE(std::all_of(passphrase.begin(), passphrase.end(), ::isxdigit));
    EXPECT_EQ(kSecurityWpa2, GetConfigSecurity(caps));
    EXPECT_EQ(GetConfigBand(caps), kBandAll);
    EXPECT_TRUE(caps.Contains<std::string>(kTetheringConfUpstreamTechProperty));
    EXPECT_FALSE(caps.Contains<std::string>(
        kTetheringConfDownstreamDeviceForTestProperty));
    EXPECT_FALSE(caps.Contains<uint32_t>(
        kTetheringConfDownstreamPhyIndexForTestProperty));
    return caps;
  }

  KeyValueStore GenerateFakeConfig(
      const std::string& ssid,
      const std::string passphrase,
      const std::optional<std::string> downstream_device_for_test =
          std::nullopt,
      const std::optional<uint32_t> downstream_phy_index_for_test =
          std::nullopt) {
    KeyValueStore config;
    SetConfigMAR(config, false);
    SetConfigAutoDisable(config, false);
    SetConfigSSID(config, ssid);
    SetConfigPassphrase(config, passphrase);
    SetConfigSecurity(config, kSecurityWpa3);
    SetConfigBand(config, kBand2GHz);
    SetConfigUpstream(config, kTypeCellular);
    if (downstream_device_for_test) {
      SetConfigDownstreamDeviceForTest(config, *downstream_device_for_test);
      EXPECT_NE(downstream_phy_index_for_test, std::nullopt);
      SetConfigDownstreamPhyIndexForTest(config,
                                         *downstream_phy_index_for_test);
    }
    return config;
  }

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

  void TetheringPrerequisite(TetheringManager* tethering_manager) {
    ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
    EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));
    ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
    ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
    EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  }

  void DownStreamDeviceEvent(TetheringManager* tethering_manager,
                             LocalDevice::DeviceEvent event,
                             LocalDevice* device) {
    tethering_manager->OnDownstreamDeviceEvent(event, device);
  }

  void OnCellularUpstreamEvent(TetheringManager* tethering_manager,
                               TetheringManager::CellularUpstreamEvent event) {
    tethering_manager->OnCellularUpstreamEvent(event);
  }

  TetheringManager::TetheringState TetheringState(
      TetheringManager* tethering_manager) {
    return tethering_manager->state_;
  }

  std::string StopReason(TetheringManager* tethering_manager) {
    return TetheringManager::StopReasonToString(
        tethering_manager->stop_reason_);
  }

  void CheckTetheringStopping(TetheringManager* tethering_manager,
                              const char* reason) {
    EXPECT_EQ(TetheringState(tethering_manager),
              TetheringManager::TetheringState::kTetheringStopping);
    EXPECT_EQ(StopReason(tethering_manager), reason);
  }

  void CheckTetheringIdle(TetheringManager* tethering_manager,
                          const char* reason) {
    EXPECT_EQ(tethering_manager->hotspot_dev_, nullptr);
    EXPECT_EQ(TetheringState(tethering_manager),
              TetheringManager::TetheringState::kTetheringIdle);
    auto status = GetStatus(tethering_manager);
    EXPECT_EQ(status.Get<std::string>(kTetheringStatusIdleReasonProperty),
              reason);
    EXPECT_TRUE(GetStartTimer(tethering_manager_).IsCancelled());
    EXPECT_TRUE(GetStopTimer(tethering_manager_).IsCancelled());
  }

  KeyValueStore GetStatus(TetheringManager* tethering_manager) {
    return tethering_manager->GetStatus();
  }

  void OnStartingTetheringTimeout(TetheringManager* tethering_manager) {
    tethering_manager->OnStartingTetheringTimeout();
  }

  void OnStartingTetheringUpdateTimeout(TetheringManager* tethering_manager,
                                        base::TimeDelta timeout) {
    tethering_manager->OnStartingTetheringUpdateTimeout(timeout);
  }

  void OnStoppingTetheringTimeout(TetheringManager* tethering_manager) {
    tethering_manager->OnStoppingTetheringTimeout();
  }

  const base::CancelableOnceClosure& GetStartTimer(
      TetheringManager* tethering_manager) {
    return tethering_manager->start_timer_callback_;
  }

  const base::CancelableOnceClosure& GetStopTimer(
      TetheringManager* tethering_manager) {
    return tethering_manager->stop_timer_callback_;
  }

  const base::CancelableOnceClosure& GetInactiveTimer(
      TetheringManager* tethering_manager) {
    return tethering_manager->inactive_timer_callback_;
  }

  const base::CancelableOnceClosure& GetUpstreamNetworkValidationTimer(
      TetheringManager* tethering_manager) {
    return tethering_manager->upstream_network_validation_timer_callback_;
  }

  void AddServiceToCellularProvider(CellularServiceRefPtr service) {
    cellular_service_provider_->AddService(service);
  }

  void OnDownstreamNetworkReady(
      TetheringManager* tethering_manager,
      base::ScopedFD fd,
      const patchpanel::Client::DownstreamNetwork& downstream_network) {
    tethering_manager->OnDownstreamNetworkReady(std::move(fd),
                                                downstream_network);
  }

  void OnUpstreamNetworkAcquired(TetheringManager* tethering_manager,
                                 TetheringManager::SetEnabledResult result) {
    tethering_manager->OnUpstreamNetworkAcquired(result, network_.get(),
                                                 service_.get());
  }

  void OnUpstreamNetworkReleased(TetheringManager* tethering_manager,
                                 bool success) {
    tethering_manager->OnUpstreamNetworkReleased(success);
  }

  void OnUpstreamNetworkStopped(TetheringManager* tethering_manager) {
    tethering_manager->OnNetworkStopped(kTestInterfaceIndex, false);
  }

  void OnUpstreamNetworkDestroyed(TetheringManager* tethering_manager) {
    tethering_manager->OnNetworkDestroyed(network_->network_id(),
                                          kTestInterfaceIndex);
  }

  void OnUpstreamNetworkValidationResult(TetheringManager* tethering_manager,
                                         const NetworkMonitor::Result& result) {
    tethering_manager->OnNetworkValidationResult(kTestInterfaceIndex, result);
  }

  void OnDeviceCreated() {
    tethering_manager_->OnDeviceCreated(hotspot_device_);
  }

  void OnDeviceCreationFailed() {
    tethering_manager_->OnDeviceCreationFailed();
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
  MockPatchpanelClient* patchpanel_;
  TetheringManager* tethering_manager_;
  MockWiFiProvider* wifi_provider_;
  MockEthernetProvider* ethernet_provider_;
  scoped_refptr<NiceMock<MockProfile>> cellular_profile_;
  MockCellularServiceProvider* cellular_service_provider_;
  MockUpstart* upstart_;
  scoped_refptr<MockHotspotDevice> hotspot_device_;
  std::unique_ptr<MockNetwork> network_;
  scoped_refptr<MockService> service_;
  MockWiFiPhy wifi_phy_;
};

TEST_F(TetheringManagerTest, GetTetheringCapabilities) {
  std::unique_ptr<NiceMock<MockWiFiPhy>> phy(
      new NiceMock<MockWiFiPhy>(kPhyIndex));
  const std::vector<const WiFiPhy*> phys = {phy.get()};
  ON_CALL(*wifi_provider_, GetPhys()).WillByDefault(Return(phys));
  ON_CALL(*phy, SupportAPMode()).WillByDefault(Return(true));
  ON_CALL(*phy, SupportAPSTAConcurrency()).WillByDefault(Return(true));
  EXPECT_CALL(*cellular_service_provider_, HardwareSupportsTethering(_))
      .WillOnce(Return(true));
  tethering_manager_->RefreshCapabilities();
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
  EXPECT_CALL(*cellular_service_provider_, HardwareSupportsTethering(_))
      .WillOnce(Return(true));

  tethering_manager_->RefreshCapabilities();
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

TEST_F(TetheringManagerTest, GetTetheringCapabilitiesWithoutCellular) {
  std::unique_ptr<NiceMock<MockWiFiPhy>> phy(
      new NiceMock<MockWiFiPhy>(kPhyIndex));
  const std::vector<const WiFiPhy*> phys = {phy.get()};
  ON_CALL(*wifi_provider_, GetPhys()).WillByDefault(Return(phys));
  ON_CALL(*phy, SupportAPMode()).WillByDefault(Return(true));
  ON_CALL(*phy, SupportAPSTAConcurrency()).WillByDefault(Return(true));
  EXPECT_CALL(*cellular_service_provider_, HardwareSupportsTethering(_))
      .WillOnce(Return(false));

  tethering_manager_->RefreshCapabilities();
  KeyValueStore caps = GetCapabilities(tethering_manager_);

  auto upstream_technologies =
      caps.Get<std::vector<std::string>>(kTetheringCapUpstreamProperty);
  EXPECT_FALSE(upstream_technologies.empty());
  EXPECT_TRUE(base::Contains(upstream_technologies, kTypeEthernet));
  EXPECT_FALSE(base::Contains(upstream_technologies, kTypeCellular));
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

TEST_F(TetheringManagerTest, TetheringConfig) {
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));

  // Check default TetheringConfig.
  VerifyDefaultTetheringConfig(tethering_manager_);

  // Fake Tethering configuration.
  KeyValueStore args = GenerateFakeConfig(kTestAPHexSSID, kTestPassword,
                                          kTestDownstreamDeviceForTest,
                                          kTestDownstreamPhyIndexForTest);

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
  EXPECT_EQ(GetConfigSSID(config), kTestAPHexSSID);
  EXPECT_EQ(GetConfigPassphrase(config), kTestPassword);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);
  EXPECT_EQ(GetConfigDownstreamDeviceForTest(config),
            kTestDownstreamDeviceForTest);
  EXPECT_EQ(GetConfigDownstreamPhyIndexForTest(config),
            kTestDownstreamPhyIndexForTest);

  // Log out user and check user's tethering config is not present.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  KeyValueStore default_config = GetConfig(tethering_manager_);
  EXPECT_NE(GetConfigSSID(default_config), kTestAPHexSSID);
  EXPECT_NE(GetConfigPassphrase(default_config), kTestPassword);

  // Log in user and check tethering config again.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  config = GetConfig(tethering_manager_);
  EXPECT_FALSE(GetConfigMAR(config));
  EXPECT_FALSE(GetConfigAutoDisable(config));
  EXPECT_EQ(GetConfigSSID(config), kTestAPHexSSID);
  EXPECT_EQ(GetConfigPassphrase(config), kTestPassword);
  EXPECT_EQ(GetConfigSecurity(config), kSecurityWpa3);
  EXPECT_EQ(GetConfigBand(config), kBand2GHz);
  EXPECT_EQ(GetConfigUpstream(config), kTypeCellular);

  // These properties are only used for testing, should not be persisted.
  EXPECT_FALSE(
      config.ContainsVariant(kTetheringConfDownstreamDeviceForTestProperty));
  EXPECT_FALSE(
      config.ContainsVariant(kTetheringConfDownstreamPhyIndexForTestProperty));
}

TEST_F(TetheringManagerTest, DefaultConfigCheck) {
  // SetEnabled proceed to starting state and persist the default config.
  ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append("user")));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kUserProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  KeyValueStore config = GetConfig(tethering_manager_);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Log out user and check a new SSID and passphrase is generated.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  KeyValueStore default_config = GetConfig(tethering_manager_);
  EXPECT_NE(GetConfigSSID(config), GetConfigSSID(default_config));
  EXPECT_NE(GetConfigPassphrase(config), GetConfigPassphrase(default_config));
  EXPECT_FALSE(default_config.ContainsVariant(
      kTetheringConfDownstreamDeviceForTestProperty));
  EXPECT_FALSE(default_config.ContainsVariant(
      kTetheringConfDownstreamPhyIndexForTestProperty));

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
  EXPECT_FALSE(new_config.ContainsVariant(
      kTetheringConfDownstreamDeviceForTestProperty));
  EXPECT_FALSE(new_config.ContainsVariant(
      kTetheringConfDownstreamPhyIndexForTestProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigLoadAndUnload) {
  // Check properties of the default tethering configuration.
  VerifyDefaultTetheringConfig(tethering_manager_);

  // Prepare faked tethering configuration stored for a fake user profile.
  FakeStore store;
  store.SetBool(TetheringManager::kStorageId, kTetheringConfAutoDisableProperty,
                true);
  store.SetBool(TetheringManager::kStorageId, kTetheringConfMARProperty, true);
  const MACAddress mac = MACAddress::CreateRandom();
  mac.Save(&store, TetheringManager::kStorageId);
  store.SetString(TetheringManager::kStorageId, kTetheringConfSSIDProperty,
                  kTestAPHexSSID);
  store.SetString(TetheringManager::kStorageId,
                  kTetheringConfPassphraseProperty, kTestPassword);
  store.SetString(TetheringManager::kStorageId, kTetheringConfSecurityProperty,
                  kSecurityWpa3);
  store.SetString(TetheringManager::kStorageId, kTetheringConfBandProperty,
                  kBand5GHz);
  store.SetString(TetheringManager::kStorageId,
                  kTetheringConfUpstreamTechProperty, kTypeCellular);
  store.SetString(TetheringManager::kStorageId,
                  kTetheringConfDownstreamDeviceForTestProperty, "wlan5");
  store.SetUint64(TetheringManager::kStorageId,
                  kTetheringConfDownstreamPhyIndexForTestProperty, 5);
  scoped_refptr<MockProfile> profile =
      new MockProfile(&manager_, "~user/profile0");
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&store));

  // Check faked properties are loaded.
  tethering_manager_->LoadConfigFromProfile(profile);
  KeyValueStore caps = GetConfig(tethering_manager_);
  EXPECT_TRUE(GetConfigMAR(caps));
  EXPECT_EQ(tethering_manager_->stable_mac_addr_, mac);
  EXPECT_TRUE(GetConfigAutoDisable(caps));
  EXPECT_EQ(kTestAPHexSSID, GetConfigSSID(caps));
  EXPECT_EQ(kTestPassword, GetConfigPassphrase(caps));
  EXPECT_EQ(kSecurityWpa3, GetConfigSecurity(caps));
  EXPECT_EQ(kBand5GHz, GetConfigBand(caps));
  EXPECT_EQ(kTypeCellular, GetConfigUpstream(caps));

  // These properties should not be loaded from persisted storage, because they
  // are only for testing.
  EXPECT_FALSE(
      caps.ContainsVariant(kTetheringConfDownstreamDeviceForTestProperty));
  EXPECT_FALSE(
      caps.ContainsVariant(kTetheringConfDownstreamPhyIndexForTestProperty));

  // Check the tethering config is reset to default properties when unloading
  // the profile.
  tethering_manager_->UnloadConfigFromProfile();
  caps = VerifyDefaultTetheringConfig(tethering_manager_);
  EXPECT_NE(kTestAPHexSSID, caps.Get<std::string>(kTetheringConfSSIDProperty));
  EXPECT_NE(kTestPassword,
            caps.Get<std::string>(kTetheringConfPassphraseProperty));
}

TEST_F(TetheringManagerTest, TetheringConfigSaveAndLoad) {
  // Load a fake tethering configuration.
  KeyValueStore config1 = GenerateFakeConfig(kTestAPHexSSID, kTestPassword,
                                             kTestDownstreamDeviceForTest,
                                             kTestDownstreamPhyIndexForTest);
  FromProperties(tethering_manager_, config1);

  // Save the fake tethering configuration
  FakeStore store;
  SaveConfig(tethering_manager_, &store);

  // These properties should not be saved to persisted storage, because they are
  // only for testing.
  EXPECT_FALSE(store.GetString(TetheringManager::kStorageId,
                               kTetheringConfDownstreamDeviceForTestProperty,
                               nullptr));
  EXPECT_FALSE(store.GetUint64(TetheringManager::kStorageId,
                               kTetheringConfDownstreamPhyIndexForTestProperty,
                               nullptr));

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

TEST_F(TetheringManagerTest, TetheringInDefaultProfile) {
  // SetEnabled fails for the default profile.
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager_, kDefaultProfile));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kDefaultProfile));
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kNotAllowed);
}

TEST_F(TetheringManagerTest, CheckReadinessCellularUpstream) {
  base::MockOnceCallback<void(TetheringManager::EntitlementStatus)> cb;
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");
  SetConfigUpstream(config, TechnologyName(Technology::kCellular));
  EXPECT_TRUE(FromProperties(tethering_manager_, config));

  // No cellular Device.
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Set one fake ethernet Device.
  auto eth = new NiceMock<MockDevice>(
      &manager_, "eth0",
      net_base::MacAddress(0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f), 1);
  ON_CALL(*eth, technology()).WillByDefault(Return(Technology::kEthernet));
  const std::vector<DeviceRefPtr> eth_devices = {eth};
  ON_CALL(manager_, FilterByTechnology(Technology::kEthernet))
      .WillByDefault(Return(eth_devices));
  auto eth_service(new MockService(&manager_));
  eth->set_selected_service_for_testing(eth_service);

  // Set one fake cellular Device.
  auto cell = MakeCellular(
      "wwan0", net_base::MacAddress(0x00, 0x01, 0x02, 0x03, 0x04, 0x05), 2);
  const std::vector<DeviceRefPtr> cell_devices = {cell};
  ON_CALL(manager_, FilterByTechnology(Technology::kCellular))
      .WillByDefault(Return(cell_devices));
  scoped_refptr<MockCellularService> cell_service =
      new MockCellularService(&manager_, cell);
  AddServiceToCellularProvider(cell_service);
  cell->set_selected_service_for_testing(cell_service);

  // Both Ethernet Service and Cellular Service are disconnected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Ethernet Service is connected, Cellular Service is disconnected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Ethernet Service is disconnected, Cellular Service is connected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*cellular_service_provider_, TetheringEntitlementCheck(_, _));
  tethering_manager_->CheckReadiness(cb.Get());
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
  Mock::VerifyAndClearExpectations(cellular_service_provider_);

  // Both Ethernet Service and Cellular Service are connected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*cellular_service_provider_, TetheringEntitlementCheck(_, _));
  tethering_manager_->CheckReadiness(cb.Get());
  DispatchPendingEvents();
}

TEST_F(TetheringManagerTest, CheckReadinessEthernetUpstream) {
  base::MockOnceCallback<void(TetheringManager::EntitlementStatus)> cb;
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");
  SetConfigUpstream(config, TechnologyName(Technology::kEthernet));
  EXPECT_TRUE(FromProperties(tethering_manager_, config));

  // No ethernet Device.
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Set one fake ethernet Device.
  auto eth = new NiceMock<MockDevice>(
      &manager_, "eth0",
      net_base::MacAddress(0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f), 1);
  ON_CALL(*eth, technology()).WillByDefault(Return(Technology::kEthernet));
  const std::vector<DeviceRefPtr> eth_devices = {eth};
  ON_CALL(manager_, FilterByTechnology(Technology::kEthernet))
      .WillByDefault(Return(eth_devices));
  auto eth_service(new MockService(&manager_));
  eth->set_selected_service_for_testing(eth_service);

  // Set one fake cellular Device.
  auto cell = MakeCellular(
      "wwan0", net_base::MacAddress(0x00, 0x01, 0x02, 0x03, 0x04, 0x05), 2);
  const std::vector<DeviceRefPtr> cell_devices = {cell};
  ON_CALL(manager_, FilterByTechnology(Technology::kCellular))
      .WillByDefault(Return(cell_devices));
  scoped_refptr<MockCellularService> cell_service =
      new MockCellularService(&manager_, cell);
  AddServiceToCellularProvider(cell_service);
  cell->set_selected_service_for_testing(cell_service);

  EXPECT_CALL(*cellular_service_provider_, TetheringEntitlementCheck(_, _))
      .Times(0);

  // Both Ethernet Service and Cellular Service are disconnected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Ethernet Service is connected, Cellular Service is disconnected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kReady));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Ethernet Service is disconnected, Cellular Service is connected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  tethering_manager_->CheckReadiness(cb.Get());
  EXPECT_CALL(
      cb,
      Run(TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable));
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Both Ethernet Service and Cellular Service are connected.
  EXPECT_CALL(*eth_service, IsConnected(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
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

  EXPECT_EQ("wrong_state",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kWrongState));

  EXPECT_EQ("upstream_failure",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kUpstreamFailure));

  EXPECT_EQ("downstream_wifi_failure",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kDownstreamWiFiFailure));

  EXPECT_EQ("network_setup_failure",
            TetheringManager::SetEnabledResultName(
                TetheringManager::SetEnabledResult::kNetworkSetupFailure));

  EXPECT_EQ("abort", TetheringManager::SetEnabledResultName(
                         TetheringManager::SetEnabledResult::kAbort));

  EXPECT_EQ("busy", TetheringManager::SetEnabledResultName(
                        TetheringManager::SetEnabledResult::kBusy));
}

TEST_F(TetheringManagerTest, StartTetheringSessionSuccessWithCellularUpstream) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Upstream network fetched.
  const NetworkMonitor::Result network_monitor_result{
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
  };
  network_->set_network_monitor_result_for_testing(network_monitor_result);
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);

  // Tethering network created.
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});

  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, StartTetheringSessionSuccessWithEthernetUpstream) {
  MockNetwork eth_network(kTestInterfaceIndex + 1, "eth0",
                          Technology::kEthernet);
  ON_CALL(eth_network, IsConnected()).WillByDefault(Return(true));
  scoped_refptr<MockService> eth_service = new MockService(&manager_);
  EXPECT_CALL(manager_, GetFirstEthernetService())
      .WillOnce(Return(eth_service));
  EXPECT_CALL(manager_, FindActiveNetworkFromService(_))
      .WillOnce(Return(&eth_network));

  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "eth0", _, _, _, _))
      .WillOnce(Return(true));

  // TetheringManager will evaluate the downstream service readiness as soon as
  // it finds the ethernet upstream network.
  ON_CALL(*hotspot_device_.get(), IsServiceUp()).WillByDefault(Return(false));

  // Change the upstream technology to ethernet.
  TetheringPrerequisite(tethering_manager_);
  KeyValueStore config =
      GenerateFakeConfig("757365725F73736964", "user_password");
  SetConfigUpstream(config, TechnologyName(Technology::kEthernet));
  EXPECT_TRUE(FromProperties(tethering_manager_, config));

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  Mock::VerifyAndClearExpectations(&manager_);

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  ON_CALL(*hotspot_device_.get(), IsServiceUp()).WillByDefault(Return(true));
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Tethering network created.
  const NetworkMonitor::Result network_monitor_result{
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
  };
  eth_network.set_network_monitor_result_for_testing(network_monitor_result);
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});

  Mock::VerifyAndClearExpectations(&manager_);
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
}

TEST_F(TetheringManagerTest,
       StartTetheringSessionTetheredNetworkImmediateFailure) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  // Tethering network creation request fails.
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(false));

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Upstream network fetched.
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);

  VerifyResult(TetheringManager::SetEnabledResult::kNetworkSetupFailure);
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamNetworkDisconnect);
}

TEST_F(TetheringManagerTest,
       StartTetheringSessionTetheredNetworkDelayedFailure) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Upstream network fetched.
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);

  // Tethering network creation request fails
  OnDownstreamNetworkReady(tethering_manager_, base::ScopedFD(-1), {});

  VerifyResult(TetheringManager::SetEnabledResult::kNetworkSetupFailure);
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamNetworkDisconnect);
}

TEST_F(TetheringManagerTest,
       StartTetheringSessionTetheredNetworkAlreadyStarted) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Tethering session is started.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);

  // Force another LocalDevice::DeviceEvent::kLinkUp event for the
  // downstream network which should be ignored.
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
}

TEST_F(TetheringManagerTest, StartTetheringSessionUpstreamNetworkNotConnected) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  Mock::VerifyAndClearExpectations(&manager_);

  // Upstream Network fetched but the the Network has disconnected.
  EXPECT_CALL(*network_, IsConnected()).WillRepeatedly(Return(false));
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);

  VerifyResult(TetheringManager::SetEnabledResult::kFailure);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamDisconnect);
}

TEST_F(TetheringManagerTest, StartTetheringSessionUpstreamNetworkNotReady) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Upstream network fetched. Network has no Internet connectivity
  const NetworkMonitor::Result network_monitor_result{
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultUnknown,
  };
  network_->set_network_monitor_result_for_testing(network_monitor_result);
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Tethering network created.
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);

  // Feed network validation result event.
  OnUpstreamNetworkValidationResult(tethering_manager_, network_monitor_result);

  // TODO(b/291845893): Normally the session is expected to fail. Change the
  // test expectations once a new tethering session properly fails if
  // TetheringManager cannot observe the upstream network is ready after a few
  // network validation retries.
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, StartTetheringSessionUpstreamNetworkHasPortal) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  // Downstream device event service up.
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());

  // Upstream network fetched. Network is in a portal state.
  const NetworkMonitor::Result network_monitor_result{
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kPortalRedirect,
      .probe_result_metric = Metrics::kPortalDetectorResultRedirectFound,
  };
  network_->set_network_monitor_result_for_testing(network_monitor_result);
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Tethering network created.
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});

  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, StartTetheringSessionBusy) {
  TetheringPrerequisite(tethering_manager_);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Start again while tethering state is starting.
  EXPECT_CALL(result_cb_, Run(TetheringManager::SetEnabledResult::kBusy));
  Enable(tethering_manager_, kPriorityForTest);
  Mock::VerifyAndClearExpectations(&result_cb_);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, StartTetheringSessionAbort) {
  TetheringPrerequisite(tethering_manager_);
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Abort.
  EXPECT_CALL(result_cb_, Run(TetheringManager::SetEnabledResult::kAbort));
  SetEnabled(tethering_manager_, false);
  Mock::VerifyAndClearExpectations(&result_cb_);
  // Send upstream tear down event
  OnUpstreamNetworkReleased(tethering_manager_, true);
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonClientStop);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, FailToCreateLocalInterface) {
  TetheringPrerequisite(tethering_manager_);
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation)
      .WillOnce(DoAll(InvokeWithoutArgs(
                          this, &TetheringManagerTest::OnDeviceCreationFailed),
                      Return(true)));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_)).Times(0);
  SetEnabledVerifyResult(
      tethering_manager_, true,
      TetheringManager::SetEnabledResult::kDownstreamWiFiFailure);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamLinkDisconnect);
}

TEST_F(TetheringManagerTest, InterfaceCreationRejected) {
  TetheringPrerequisite(tethering_manager_);
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation)
      .WillOnce(Return(false));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_)).Times(0);
  SetEnabledVerifyResult(
      tethering_manager_, true,
      TetheringManager::SetEnabledResult::kConcurrencyNotSupported);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_, kTetheringIdleReasonResourceBusy);
}

TEST_F(TetheringManagerTest, FailToConfigureService) {
  TetheringPrerequisite(tethering_manager_);
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation)
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &TetheringManagerTest::OnDeviceCreated),
                Return(true)));
  EXPECT_CALL(*hotspot_device_.get(), ConfigureService(_))
      .WillOnce(Return(false));
  EXPECT_CALL(*hotspot_device_.get(), DeconfigureService())
      .WillOnce(Return(true));

  SetEnabledVerifyResult(
      tethering_manager_, true,
      TetheringManager::SetEnabledResult::kDownstreamWiFiFailure);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamLinkDisconnect);
}

TEST_F(TetheringManagerTest, FailToFetchUpstreamNetwork) {
  TetheringPrerequisite(tethering_manager_);
  Enable(tethering_manager_, kPriorityForTest);
  // Upstream network fetch failed.
  OnUpstreamNetworkAcquired(
      tethering_manager_,
      TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable);
  VerifyResult(
      TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamNotAvailable);
}

TEST_F(TetheringManagerTest, UserStopTetheringSession) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  SetEnabledVerifyResult(tethering_manager_, false,
                         TetheringManager::SetEnabledResult::kSuccess);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonClientStop);
}

TEST_F(TetheringManagerTest, TetheringStopWhenUserLogout) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Log out user should also stop active tethering session and put tethering
  // state to idle.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  CheckTetheringStopping(tethering_manager_, kTetheringIdleReasonUserExit);
}

TEST_F(TetheringManagerTest, DeviceEventInterfaceDisabled) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kInterfaceDisabled,
                        hotspot_device_.get());
  DispatchPendingEvents();
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamLinkDisconnect);
}

TEST_F(TetheringManagerTest, DeviceEventServiceDown) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkDown,
                        hotspot_device_.get());
  DispatchPendingEvents();
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamLinkDisconnect);
}

TEST_F(TetheringManagerTest, UpstreamNetworkStopped) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  OnUpstreamNetworkStopped(tethering_manager_);
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamDisconnect);
}

TEST_F(TetheringManagerTest, UpstreamNetworkDestroyed) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // State change from active to stopping.
  EXPECT_CALL(manager_, TetheringStatusChanged());
  OnUpstreamNetworkDestroyed(tethering_manager_);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamDisconnect);
}

TEST_F(TetheringManagerTest, InterfaceDisabledWhenTetheringIsStarting) {
  TetheringPrerequisite(tethering_manager_);

  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kInterfaceDisabled,
                        hotspot_device_.get());
  VerifyResult(TetheringManager::SetEnabledResult::kDownstreamWiFiFailure);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonDownstreamLinkDisconnect);
}

TEST_F(TetheringManagerTest, UpstreamNetworkValidationFails) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  Enable(tethering_manager_, kPriorityForTest);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});

  // Downstream network is fully configured. Upstream network is acquired but
  // not yet ready. The tethering session is now started, with the upstream
  // network validation timer active.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  EXPECT_FALSE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);

  // Feed negative network validation result event. TetheringManager is still
  // leaving a chance for the upstream network validation to succeed.
  const NetworkMonitor::Result network_monitor_result{
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultConnectionFailure,
  };
  network_->set_network_monitor_result_for_testing(network_monitor_result);
  OnUpstreamNetworkValidationResult(tethering_manager_, network_monitor_result);

  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  EXPECT_FALSE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());
  Mock::VerifyAndClearExpectations(&manager_);

  // Force the network validation timer to expires
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  GetUpstreamNetworkValidationTimer(tethering_manager_).callback().Run();
  EXPECT_TRUE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());

  // The Tethering session has stopped.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamNoInternet);
}

TEST_F(TetheringManagerTest, UpstreamNetworkLosesInternetAccess) {
  TetheringPrerequisite(tethering_manager_);

  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  EXPECT_CALL(*patchpanel_, CreateTetheredNetwork("ap0", "wwan0", _, _, _, _))
      .WillOnce(Return(true));

  // becomes active.
  const NetworkMonitor::Result connected_result{
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
  };
  network_->set_network_monitor_result_for_testing(connected_result);

  Enable(tethering_manager_, kPriorityForTest);
  DownStreamDeviceEvent(tethering_manager_, LocalDevice::DeviceEvent::kLinkUp,
                        hotspot_device_.get());
  OnUpstreamNetworkAcquired(tethering_manager_,
                            TetheringManager::SetEnabledResult::kSuccess);
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  OnDownstreamNetworkReady(tethering_manager_, MakeFd(),
                           {.network_id = kTetheredNetworkId});

  // Downstream network is fully configured. Upstream network is acquired
  // readyand . The tethering session is now started, without the upstream
  // network validation timer.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  EXPECT_TRUE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());
  VerifyResult(TetheringManager::SetEnabledResult::kSuccess);
  Mock::VerifyAndClearExpectations(&manager_);

  // The upstream network loses Internet access. The upstream network validation
  // timer becomes active.
  const NetworkMonitor::Result not_connected_result{
      .num_attempts = 2,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultConnectionFailure,
  };
  network_->set_network_monitor_result_for_testing(not_connected_result);
  OnUpstreamNetworkValidationResult(tethering_manager_, not_connected_result);

  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
  EXPECT_FALSE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());

  // Force the upstream network validation timer to expires
  EXPECT_CALL(manager_, TetheringStatusChanged()).Times(1);
  GetUpstreamNetworkValidationTimer(tethering_manager_).callback().Run();
  EXPECT_TRUE(
      GetUpstreamNetworkValidationTimer(tethering_manager_).IsCancelled());

  // The Tethering session has stopped.
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamNoInternet);
  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(TetheringManagerTest, DeviceEventPeerConnectedDisconnected) {
  TetheringPrerequisite(tethering_manager_);
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
  Mock::VerifyAndClearExpectations(&manager_);
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
  const std::vector<net_base::MacAddress> clients = {
      net_base::MacAddress(0x00, 0x11, 0x22, 0x33, 0x44, 0x55),
      net_base::MacAddress(0x00, 0x11, 0x22, 0x33, 0x44, 0x66)};
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
  // Inactive timer is not triggered when tethering is not active.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
  // Inactive timer should be armed when tethering is active and no client is
  // connected.
  EXPECT_FALSE(GetInactiveTimer(tethering_manager_).IsCancelled());

  // Connect client to the hotspot.
  std::vector<net_base::MacAddress> clients = {
      net_base::MacAddress(0x00, 0x11, 0x22, 0x33, 0x44, 0x55)};
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

TEST_F(TetheringManagerTest, TetheringStartTimer) {
  // Start tethering.
  TetheringPrerequisite(tethering_manager_);
  EXPECT_TRUE(GetStartTimer(tethering_manager_).IsCancelled());
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_FALSE(GetStartTimer(tethering_manager_).IsCancelled());
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Tethering start timeout
  OnStartingTetheringTimeout(tethering_manager_);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_, kTetheringIdleReasonStartTimeout);
}

TEST_F(TetheringManagerTest, TetheringStartTimerUpdated) {
  // Start tethering.
  TetheringPrerequisite(tethering_manager_);
  EXPECT_TRUE(GetStartTimer(tethering_manager_).IsCancelled());
  Enable(tethering_manager_, kPriorityForTest);
  EXPECT_FALSE(GetStartTimer(tethering_manager_).IsCancelled());
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Timeout updated
  OnStartingTetheringUpdateTimeout(tethering_manager_, base::Seconds(20));
  EXPECT_FALSE(GetStartTimer(tethering_manager_).IsCancelled());
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringStarting);

  // Tethering start timeout
  OnStartingTetheringTimeout(tethering_manager_);
  // Expect stopping state: the attempt will be aborted.
  CheckTetheringStopping(tethering_manager_, kTetheringIdleReasonStartTimeout);
}

TEST_F(TetheringManagerTest, TetheringStopTimer) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
  // Stop tethering.
  EXPECT_TRUE(GetStopTimer(tethering_manager_).IsCancelled());
  SetEnabled(tethering_manager_, false);
  EXPECT_FALSE(GetStopTimer(tethering_manager_).IsCancelled());
  // Tethering stop timeout
  OnStoppingTetheringTimeout(tethering_manager_);
  VerifyResult(TetheringManager::SetEnabledResult::kUpstreamFailure);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonClientStop);
}

TEST_F(TetheringManagerTest, MARWithSSIDChange) {
  TetheringPrerequisite(tethering_manager_);

  // Upon initialization TetheringManager generates some config.  Let's take
  // a snapshot of the SSID/MAC (to test if MAC changes upon SSID change).
  const std::string ini_ssid = tethering_manager_->hex_ssid_;
  const net_base::MacAddress ini_mac =
      tethering_manager_->stable_mac_addr_.address().value();

  // Change SSID to cause regeneration of MAC address.
  KeyValueStore args = GenerateFakeConfig(kTestAPHexSSID, kTestPassword);
  // Turn off randomization.
  SetConfigMAR(args, false);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, args));
  const net_base::MacAddress mac =
      tethering_manager_->stable_mac_addr_.address().value();
  ASSERT_NE(ini_ssid, kTestAPHexSSID);
  EXPECT_NE(ini_mac, mac);

  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(mac, _, _));
  // Test 1st argument for RequestHotspotDeviceCreation (MAC as a hex-string).
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  Enable(tethering_manager_, kPriorityForTest);
}

MATCHER_P(IsContained, container, "") {
  return base::Contains(container, arg);
}

TEST_F(TetheringManagerTest, MARWithTetheringRestart) {
  TetheringPrerequisite(tethering_manager_);
  std::set<net_base::MacAddress> known_macs;
  known_macs.insert(tethering_manager_->stable_mac_addr_.address().value());

  auto tether_onoff = [&]() {
    EXPECT_CALL(*wifi_provider_,
                CreateHotspotDevice(Not(IsContained(known_macs)), _, _))
        .WillOnce(DoAll(
            WithArg<0>(Invoke([&](auto mac) { known_macs.insert(mac); })),
            InvokeWithoutArgs(this, &TetheringManagerTest::OnDeviceCreated)));
    EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
        .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
          std::move(create_device_cb).Run();
          return true;
        });
    SetEnabledVerifyResult(tethering_manager_, true,
                           TetheringManager::SetEnabledResult::kSuccess);
    EXPECT_EQ(TetheringState(tethering_manager_),
              TetheringManager::TetheringState::kTetheringActive);
    SetEnabledVerifyResult(tethering_manager_, false,
                           TetheringManager::SetEnabledResult::kSuccess);
    CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonClientStop);
  };

  for (int i = 0; i < 4; ++i) {
    tether_onoff();
  }
}

TEST_F(TetheringManagerTest, CheckMACStored) {
  TetheringPrerequisite(tethering_manager_);

  // Change SSID to cause regeneration of MAC address.
  KeyValueStore args;
  SetConfigSSID(args, kTestAPHexSSID);
  // Turn off randomization to check the MAC is being used at the end.
  SetConfigMAR(args, false);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, args));

  const net_base::MacAddress ini_mac =
      tethering_manager_->stable_mac_addr_.address().value();

  // Now PopProfile and check that MAC is different.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager_, kUserProfile));
  EXPECT_NE(ini_mac, tethering_manager_->stable_mac_addr_.address());

  // Repush the profile and check that MAC returns to its original value.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager_, kUserProfile));
  EXPECT_EQ(ini_mac, tethering_manager_->stable_mac_addr_.address());

  // And test that it is actually used.
  EXPECT_CALL(*wifi_provider_, CreateHotspotDevice(Eq(ini_mac), _, _))
      .WillOnce(
          InvokeWithoutArgs(this, &TetheringManagerTest::OnDeviceCreated));
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });

  Enable(tethering_manager_, kPriorityForTest);
}

TEST_F(TetheringManagerTest, OnCellularUpstreamEvent) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);
  OnCellularUpstreamEvent(
      tethering_manager_,
      TetheringManager::CellularUpstreamEvent::kUserNoLongerEntitled);
  CheckTetheringStopping(tethering_manager_,
                         kTetheringIdleReasonUpstreamDisconnect);
}

TEST_F(TetheringManagerTest, ChangeSSIDWhileIdle) {
  TetheringPrerequisite(tethering_manager_);
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonInitialState);
  // Change SSID and set to TetheringConfig.
  KeyValueStore config = GetConfig(tethering_manager_);
  SetConfigSSID(config, kTestAPHexSSID);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  DispatchPendingEvents();
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonInitialState);
}

TEST_F(TetheringManagerTest, ChangeSSIDWhileActive) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Change SSID and set to TetheringConfig.
  KeyValueStore config = GetConfig(tethering_manager_);
  SetConfigSSID(config, kTestAPHexSSID);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Changing SSID should not touch the upstream network.
  EXPECT_CALL(*cellular_service_provider_, ReleaseTetheringNetwork(_, _))
      .Times(0);
  DispatchPendingEvents();
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringRestarting);
}

TEST_F(TetheringManagerTest, ChangeUpstreamTechWhileActive) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Change upstream tech from cellular to eth and set to TetheringConfig.
  KeyValueStore config = GetConfig(tethering_manager_);
  SetConfigUpstream(config, TechnologyName(Technology::kEthernet));
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Changing upstream technology should release the upstream network.
  EXPECT_CALL(*cellular_service_provider_, ReleaseTetheringNetwork(_, _))
      .WillOnce(Return());
  DispatchPendingEvents();
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringRestarting);
}

TEST_F(TetheringManagerTest, ChangeAutoDisableWhileIdle) {
  TetheringPrerequisite(tethering_manager_);
  KeyValueStore config = GetConfig(tethering_manager_);
  SetConfigAutoDisable(config, false);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonInitialState);
  SetConfigAutoDisable(config, true);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  CheckTetheringIdle(tethering_manager_, kTetheringIdleReasonInitialState);
}

TEST_F(TetheringManagerTest, ChangeAutoDisableWhileActive) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Change auto disable from true to false and set to TetheringConfig.
  KeyValueStore config = GetConfig(tethering_manager_);
  SetConfigAutoDisable(config, false);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Set auto disable to false will terminate the inactive timer.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  // No session restart is triggered.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);

  // Change auto disable from false to true and set to TetheringConfig.
  SetConfigAutoDisable(config, true);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Set auto disable to true will restart the inactive timer.
  EXPECT_FALSE(GetInactiveTimer(tethering_manager_).IsCancelled());
  // No session restart is triggered.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);

  // Connect client to the hotspot.
  const std::vector<net_base::MacAddress> clients = {
      net_base::MacAddress(0x00, 0x11, 0x22, 0x33, 0x44, 0x55)};
  ON_CALL(*hotspot_device_.get(), GetStations()).WillByDefault(Return(clients));
  DownStreamDeviceEvent(tethering_manager_,
                        LocalDevice::DeviceEvent::kPeerConnected,
                        hotspot_device_.get());
  DispatchPendingEvents();

  // Change auto disable from true to false and set to TetheringConfig.
  SetConfigAutoDisable(config, false);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Set auto disable to false will terminate the inactive timer.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  // No session restart is triggered.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);

  // Change auto disable from false to true and set to TetheringConfig.
  SetConfigAutoDisable(config, true);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // Set auto disable to true will not restart the inactive timer if there is
  // client connected to the hotspot.
  EXPECT_TRUE(GetInactiveTimer(tethering_manager_).IsCancelled());
  // No session restart is triggered.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
}

TEST_F(TetheringManagerTest, SetConfigWithNoChangeWhileActive) {
  TetheringPrerequisite(tethering_manager_);
  SetEnabledVerifyResult(tethering_manager_, true,
                         TetheringManager::SetEnabledResult::kSuccess);

  // Change nothing and set to TetheringConfig.
  KeyValueStore config = GetConfig(tethering_manager_);
  EXPECT_TRUE(SetAndPersistConfig(tethering_manager_, config));
  // No session restart is triggered.
  EXPECT_EQ(TetheringState(tethering_manager_),
            TetheringManager::TetheringState::kTetheringActive);
}

}  // namespace shill
