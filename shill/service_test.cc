// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/service.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/single_thread_task_runner.h>
#include <base/test/bind.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "shill/dbus/dbus_control.h"
#include "shill/error.h"
#include "shill/ethernet/ethernet_service.h"
#include "shill/ipconfig.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_device.h"
#include "shill/mock_eap_credentials.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_power_manager.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/mock_time.h"
#include "shill/network/http_request.h"
#include "shill/network/mock_network.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/service_property_change_test.h"
#include "shill/service_under_test.h"
#include "shill/store/fake_store.h"
#include "shill/store/property_store_test.h"
#include "shill/testing.h"

using net_base::IPAddress;
using net_base::IPCIDR;
using net_base::IPv4Address;
using net_base::IPv4CIDR;
using net_base::IPv6Address;
using net_base::IPv6CIDR;
using net_base::NetworkConfig;

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DefaultValue;
using testing::DoAll;
using testing::Eq;
using testing::Ge;
using testing::HasSubstr;
using testing::Mock;
using testing::NiceMock;
using testing::Pointer;
using testing::Return;
using testing::ReturnNull;
using testing::ReturnRef;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Test;
using testing::Values;
using testing::WithArgs;

namespace {
const char kConnectDisconnectReason[] = "RPC";
const char kGUID[] = "guid";
const char kIfName[] = "testdevice";
}  // namespace

namespace shill {

class ServiceTest : public PropertyStoreTest {
 public:
  ServiceTest()
      : mock_manager_(control_interface(), dispatcher(), metrics()),
        service_(new ServiceUnderTest(&mock_manager_)),
        service2_(new ServiceUnderTest(&mock_manager_)),
        storage_id_(ServiceUnderTest::kStorageId),
        power_manager_(new MockPowerManager(control_interface())) {
    ON_CALL(*control_interface(), CreatePowerManagerProxy(_, _, _))
        .WillByDefault(ReturnNull());

    service_->disconnects_.time_ = &time_;
    service_->misconnects_.time_ = &time_;
    DefaultValue<Timestamp>::Set(Timestamp());
    service_->eap_ = std::make_unique<NiceMock<MockEapCredentials>>();
    mock_manager_.running_ = true;
    mock_manager_.set_power_manager(power_manager_);  // Passes ownership.
  }

  ~ServiceTest() override = default;

  MOCK_METHOD(void, TestCallback, (const Error&));

 protected:
  using MockProfileRefPtr = scoped_refptr<MockProfile>;

  ServiceMockAdaptor* GetAdaptor() {
    return static_cast<ServiceMockAdaptor*>(service_->adaptor());
  }

  std::string GetFriendlyName() { return service_->friendly_name(); }

  void SetManagerRunning(bool running) { mock_manager_.running_ = running; }

  void SetSuspending(bool suspending) {
    power_manager_->suspending_ = suspending;
  }

  bool GetExplicitlyDisconnected() const {
    return service_->explicitly_disconnected_;
  }

  void SetExplicitlyDisconnected(bool explicitly) {
    service_->explicitly_disconnected_ = explicitly;
  }

  void SetStateField(Service::ConnectState state) { service_->state_ = state; }

  Service::ConnectState GetPreviousState() const {
    return service_->previous_state_;
  }

  void SetTechnology(Technology technology) {
    service_->technology_ = technology;
  }

  void NoteFailureEvent() { service_->NoteFailureEvent(); }

  EventHistory* GetDisconnects() { return &service_->disconnects_; }
  EventHistory* GetMisconnects() { return &service_->misconnects_; }

  Timestamp GetTimestamp(int monotonic_seconds,
                         int boottime_seconds,
                         const std::string& wall_clock) {
    struct timeval monotonic = {.tv_sec = monotonic_seconds, .tv_usec = 0};
    struct timeval boottime = {.tv_sec = boottime_seconds, .tv_usec = 0};
    return Timestamp(monotonic, boottime, wall_clock);
  }

  void PushTimestamp(EventHistory* events,
                     int monotonic_seconds,
                     int boottime_seconds,
                     const std::string& wall_clock) {
    events->RecordEventInternal(
        GetTimestamp(monotonic_seconds, boottime_seconds, wall_clock));
  }

  int GetDisconnectsMonitorSeconds() {
    return Service::kDisconnectsMonitorDuration.InSeconds();
  }

  int GetMisconnectsMonitorSeconds() {
    return Service::kMisconnectsMonitorDuration.InSeconds();
  }

  int GetMaxDisconnectEventHistory() {
    return Service::kMaxDisconnectEventHistory;
  }

  int GetMaxMisconnectEventHistory() {
    return Service::kMaxMisconnectEventHistory;
  }

  bool GetAutoConnect(Error* error) { return service_->GetAutoConnect(error); }

  void ClearAutoConnect(Error* error) { service_->ClearAutoConnect(error); }

  bool IsAutoConnectable(const char** reason) {
    return service_->IsAutoConnectable(reason);
  }

  bool SetAutoConnectFull(bool connect, Error* error) {
    return service_->SetAutoConnectFull(connect, error);
  }

  const base::CancelableOnceClosure& GetPendingConnectTask() {
    return service_->pending_connect_task_;
  }

  bool HasPendingConnect() { return !GetPendingConnectTask().IsCancelled(); }

  bool SortingOrderIs(const ServiceRefPtr& service0,
                      const ServiceRefPtr& service1,
                      bool should_compare_connectivity_state) {
    return Service::Compare(service0, service1,
                            should_compare_connectivity_state,
                            technology_order_for_sorting_)
        .first;
  }

  bool DefaultSortingOrderIs(const ServiceRefPtr& service0,
                             const ServiceRefPtr& service1) {
    const bool kShouldCompareConnectivityState = true;
    return SortingOrderIs(service0, service1, kShouldCompareConnectivityState);
  }

  std::optional<base::TimeDelta> GetTimeSinceFailed() {
    // Wait 1 MS before calling GetTimeSinceFailed.
    base::test::TestFuture<void> future;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, future.GetCallback(), base::Milliseconds(1));
    EXPECT_TRUE(future.Wait());
    return service_->GetTimeSinceFailed();
  }

  patchpanel::Client::TrafficCounter CreateCounter(
      patchpanel::Client::TrafficVector counters,
      patchpanel::Client::TrafficSource source,
      const std::string& device_name) {
    patchpanel::Client::TrafficCounter counter;
    counter.traffic = counters;
    counter.source = source;
    counter.ifname = device_name;
    return counter;
  }

  bool SetAnyPropertyAndEnsureSuccess(const std::string& name,
                                      const brillo::Any& value) {
    Error error;
    service_->mutable_store()->SetAnyProperty(name, value, &error);
    return error.IsSuccess();
  }

  NiceMock<MockManager> mock_manager_;
  NiceMock<MockTime> time_;
  scoped_refptr<ServiceUnderTest> service_;
  scoped_refptr<ServiceUnderTest> service2_;
  std::string storage_id_;
  MockPowerManager* power_manager_;  // Owned by |mock_manager_|.
  std::vector<Technology> technology_order_for_sorting_;

  void SetUplinkSpeedKbps(uint32_t uplink_speed_kbps) {
    service_->SetUplinkSpeedKbps(uplink_speed_kbps);
  }

  void SetDownlinkSpeedKbps(uint32_t downlink_speed_kbps) {
    service_->SetDownlinkSpeedKbps(downlink_speed_kbps);
  }

  uint32_t uplink_speed_kbps() { return service_->uplink_speed_kbps(); }

  uint32_t downlink_speed_kbps() { return service_->downlink_speed_kbps(); }
};

class AllMockServiceTest : public testing::Test {
 public:
  AllMockServiceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        service_(new ServiceUnderTest(&manager_)) {}
  ~AllMockServiceTest() override = default;

 protected:
  MockControl control_interface_;
  StrictMock<MockEventDispatcher> dispatcher_;
  NiceMock<MockMetrics> metrics_;
  MockManager manager_;
  scoped_refptr<ServiceUnderTest> service_;
};

TEST_F(ServiceTest, Constructor) {
  EXPECT_TRUE(service_->save_credentials_);
  EXPECT_EQ(Service::CheckPortalState::kAutomatic, service_->check_portal());
  EXPECT_EQ("auto", service_->GetCheckPortal(nullptr));
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_FALSE(service_->has_ever_connected());
  EXPECT_EQ(0, service_->previous_error_serial_number_);
  EXPECT_EQ("", service_->previous_error_);
}

TEST_F(ServiceTest, CalculateState) {
  service_->state_ = Service::kStateConnected;
  Error error;
  EXPECT_EQ(kStateReady, service_->CalculateState(&error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, CalculateTechnology) {
  service_->technology_ = Technology::kWiFi;
  Error error;
  EXPECT_EQ(kTypeWifi, service_->CalculateTechnology(&error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, GetProperties) {
  {
    brillo::VariantDictionary props;
    Error error;
    std::string expected("true");
    service_->mutable_store()->SetStringProperty(kCheckPortalProperty, expected,
                                                 &error);
    EXPECT_TRUE(service_->store().GetProperties(&props, &error));
    ASSERT_FALSE(props.find(kCheckPortalProperty) == props.end());
    EXPECT_TRUE(props[kCheckPortalProperty].IsTypeCompatible<std::string>());
    EXPECT_EQ(props[kCheckPortalProperty].Get<std::string>(), expected);
  }
  {
    brillo::VariantDictionary props;
    Error error;
    bool expected = true;
    service_->mutable_store()->SetBoolProperty(kAutoConnectProperty, expected,
                                               &error);
    EXPECT_TRUE(service_->store().GetProperties(&props, &error));
    ASSERT_FALSE(props.find(kAutoConnectProperty) == props.end());
    EXPECT_TRUE(props[kAutoConnectProperty].IsTypeCompatible<bool>());
    EXPECT_EQ(props[kAutoConnectProperty].Get<bool>(), expected);
  }
  {
    brillo::VariantDictionary props;
    Error error;
    EXPECT_TRUE(service_->store().GetProperties(&props, &error));
    ASSERT_FALSE(props.find(kConnectableProperty) == props.end());
    EXPECT_TRUE(props[kConnectableProperty].IsTypeCompatible<bool>());
    EXPECT_EQ(props[kConnectableProperty].Get<bool>(), true);
  }
  {
    brillo::VariantDictionary props;
    Error error;
    int32_t expected = 127;
    service_->mutable_store()->SetInt32Property(kPriorityProperty, expected,
                                                &error);
    EXPECT_TRUE(service_->store().GetProperties(&props, &error));
    ASSERT_FALSE(props.find(kPriorityProperty) == props.end());
    EXPECT_TRUE(props[kPriorityProperty].IsTypeCompatible<int32_t>());
    EXPECT_EQ(props[kPriorityProperty].Get<int32_t>(), expected);
  }
  {
    brillo::VariantDictionary props;
    Error error;
    service_->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kDeviceProperty) == props.end());
    EXPECT_TRUE(props[kDeviceProperty].IsTypeCompatible<dbus::ObjectPath>());
    EXPECT_EQ(props[kDeviceProperty].Get<dbus::ObjectPath>(),
              ServiceUnderTest::kRpcId);
  }
}

TEST_F(ServiceTest, SetProperty) {
  {
    EXPECT_TRUE(SetAnyPropertyAndEnsureSuccess(kSaveCredentialsProperty,
                                               PropertyStoreTest::kBoolV));
  }
  {
    const int32_t priority = 1;
    EXPECT_TRUE(SetAnyPropertyAndEnsureSuccess(kPriorityProperty,
                                               brillo::Any(priority)));
  }
  {
    const std::string guid("not default");
    EXPECT_TRUE(
        SetAnyPropertyAndEnsureSuccess(kGuidProperty, brillo::Any(guid)));
  }
  // Ensure that EAP properties cannot be set on services with no EAP
  // credentials.  Use service2_ here since we're have some code in
  // ServiceTest::SetUp() that fiddles with service_->eap_.
  std::string eap("eap eep eip!");
  {
    Error error;
    service2_->mutable_store()->SetAnyProperty(kEapMethodProperty,
                                               brillo::Any(eap), &error);
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidProperty, error.type());
  }
  {
    // Now plumb in eap credentials, and try again.
    Error error;
    service2_->SetEapCredentials(new EapCredentials());
    service2_->mutable_store()->SetAnyProperty(kEapMethodProperty,
                                               brillo::Any(eap), &error);
    EXPECT_TRUE(error.IsSuccess());
  }
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  {
    Error error;
    service_->mutable_store()->SetAnyProperty(
        kConnectableProperty, PropertyStoreTest::kBoolV, &error);
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  {
    bool auto_connect = true;
    Error error;
    service_->mutable_store()->SetAnyProperty(
        kAutoConnectProperty, brillo::Any(auto_connect), &error);
    EXPECT_TRUE(error.IsSuccess());
  }
  // Ensure that we can perform a trivial set of the Name property (to its
  // current value) but an attempt to set the property to a different value
  // fails.
  {
    Error error;
    service_->mutable_store()->SetAnyProperty(
        kNameProperty, brillo::Any(GetFriendlyName()), &error);
    EXPECT_FALSE(error.IsFailure());
  }
  {
    Error error;
    service_->mutable_store()->SetAnyProperty(
        kNameProperty, PropertyStoreTest::kStringV, &error);
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
}

TEST_F(ServiceTest, GetLoadableStorageIdentifier) {
  FakeStore storage;
  EXPECT_EQ("", service_->GetLoadableStorageIdentifier(storage));
  // Setting any property will add an entry for |storage_id_|.
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);
  EXPECT_EQ(storage_id_, service_->GetLoadableStorageIdentifier(storage));
}

TEST_F(ServiceTest, IsLoadableFrom) {
  FakeStore storage;
  EXPECT_FALSE(service_->IsLoadableFrom(storage));
  // Setting any property will add an entry for |storage_id_|.
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);
  EXPECT_TRUE(service_->IsLoadableFrom(storage));
}

class ServiceWithOnEapCredentialsChangedOverride : public ServiceUnderTest {
 public:
  ServiceWithOnEapCredentialsChangedOverride(Manager* manager,
                                             EapCredentials* eap)
      : ServiceUnderTest(manager) {
    SetEapCredentials(eap);
  }
  void OnEapCredentialsChanged(Service::UpdateCredentialsReason) override {
    SetHasEverConnected(false);
  }
};

TEST_F(ServiceTest, LoadTrafficCounters) {
  FakeStore storage;

  patchpanel::Client::TrafficVector kUserCounters = {
      .rx_bytes = 1234, .tx_bytes = 3456, .rx_packets = 10, .tx_packets = 20};
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kUser,
                        Service::kStorageTrafficCounterRxBytesSuffix),
                    1234);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kUser,
                        Service::kStorageTrafficCounterTxBytesSuffix),
                    3456);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kUser,
                        Service::kStorageTrafficCounterRxPacketsSuffix),
                    10);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kUser,
                        Service::kStorageTrafficCounterTxPacketsSuffix),
                    20);

  patchpanel::Client::TrafficVector kChromeCounters = {
      .rx_bytes = 567, .tx_bytes = 975, .rx_packets = 20, .tx_packets = 90};
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kChrome,
                        Service::kStorageTrafficCounterRxBytesSuffix),
                    567);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kChrome,
                        Service::kStorageTrafficCounterTxBytesSuffix),
                    975);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kChrome,
                        Service::kStorageTrafficCounterRxPacketsSuffix),
                    20);
  storage.SetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kChrome,
                        Service::kStorageTrafficCounterTxPacketsSuffix),
                    90);

  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(2, service_->current_total_traffic_counters().size());
  EXPECT_EQ(kUserCounters, service_->current_total_traffic_counters()
                               [patchpanel::Client::TrafficSource::kUser]);
  EXPECT_EQ(kChromeCounters, service_->current_total_traffic_counters()
                                 [patchpanel::Client::TrafficSource::kChrome]);
}

TEST_F(ServiceTest, Load) {
  FakeStore storage;
  const int kPriority = 20;
  const std::string kProxyConfig("proxy-config");
  const std::string kUIData("ui-data");
  storage.SetString(storage_id_, Service::kStorageCheckPortal, "false");
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);
  storage.SetBool(storage_id_, Service::kStorageHasEverConnected, true);
  storage.SetInt(storage_id_, Service::kStoragePriority, kPriority);
  storage.SetString(storage_id_, Service::kStorageProxyConfig, kProxyConfig);
  storage.SetString(storage_id_, Service::kStorageUIData, kUIData);
  storage.SetString(storage_id_, Service::kStorageProbeUrlHint,
                    "http://www.url.com");

  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(Service::CheckPortalState::kFalse, service_->check_portal());
  EXPECT_EQ("false", service_->GetCheckPortal(nullptr));
  EXPECT_EQ(kGUID, service_->guid_);
  EXPECT_TRUE(service_->has_ever_connected_);
  EXPECT_EQ(kProxyConfig, service_->proxy_config_);
  EXPECT_EQ(kUIData, service_->ui_data_);
  EXPECT_EQ(*net_base::HttpUrl::CreateFromString("http://www.url.com"),
            service_->probe_url_hint());

  // Removing the storage entry should cause the service to fail to load.
  storage.DeleteGroup(storage_id_);
  EXPECT_FALSE(service_->Load(&storage));

  // Set an empty GUID to add a storage entry so that the service loads.
  storage.SetString(storage_id_, Service::kStorageGUID, "");

  // Assure that parameters are set to default if not available in the profile.
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(Service::CheckPortalState::kAutomatic, service_->check_portal());
  EXPECT_EQ("auto", service_->GetCheckPortal(nullptr));
  EXPECT_EQ("", service_->guid_);
  EXPECT_EQ("", service_->proxy_config_);
  EXPECT_EQ("", service_->ui_data_);
}

TEST_F(ServiceTest, LoadEap) {
  FakeStore storage;

  const char kIdentity[] = "identity";
  storage.SetString(storage_id_, EapCredentials::kStorageCredentialEapIdentity,
                    kIdentity);

  auto* eap = new EapCredentials();
  ASSERT_FALSE(service2_->eap());
  service2_->SetEapCredentials(eap);
  service2_->SetHasEverConnected(true);
  EXPECT_TRUE(service2_->has_ever_connected());

  EXPECT_TRUE(service2_->Load(&storage));
  EXPECT_EQ(service2_->eap()->identity(), kIdentity);

  std::string identity;
  EXPECT_TRUE(storage.GetString(
      storage_id_, EapCredentials::kStorageCredentialEapIdentity, &identity));
  EXPECT_EQ(identity, kIdentity);

  // has_ever_connected_ is unaffected when loading eap credentials.
  EXPECT_TRUE(service2_->has_ever_connected());
}

TEST_F(ServiceTest, GetEapPassphrase) {
  std::string kPassword = "random-string";
  auto* eap = new EapCredentials();
  eap->set_password(kPassword);
  service2_->SetEapCredentials(eap);

  Error error;
  std::string password = service2_->GetEapPassphrase(&error);
  EXPECT_EQ(password, kPassword);
}

TEST_F(ServiceTest, GetEapPassphraseNonEap) {
  Error error;
  std::string password = service_->GetEapPassphrase(&error);
  EXPECT_TRUE(password.empty());
  EXPECT_FALSE(error.IsSuccess());
}

TEST_F(ServiceTest, LoadFail) {
  FakeStore storage;
  EXPECT_FALSE(service_->Load(&storage));
}

TEST_F(ServiceTest, LoadAutoConnect) {
  FakeStore storage;
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);

  // AutoConnect is unset.
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(service_->retain_auto_connect());

  // AutoConnect is false.
  storage.SetBool(storage_id_, Service::kStorageAutoConnect, false);
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());

  // AutoConnect is true.
  storage.SetBool(storage_id_, Service::kStorageAutoConnect, true);
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());
}

TEST_F(ServiceTest, SaveString) {
  FakeStore storage;
  static const char kKey[] = "test-key";
  static const char kData[] = "test-data";

  // Test setting kKey to a value.
  service_->SaveStringOrClear(&storage, storage_id_, kKey, kData);
  std::string data;
  EXPECT_TRUE(storage.GetString(storage_id_, kKey, &data));
  EXPECT_EQ(data, kData);

  // Setting kKey to an empty value should delete the entry
  service_->SaveStringOrClear(&storage, storage_id_, kKey, "");
  EXPECT_FALSE(storage.GetString(storage_id_, kKey, &data));
}

TEST_F(ServiceTest, SaveTrafficCounters) {
  FakeStore storage;
  patchpanel::Client::TrafficVector kVPNCounters = {
      .rx_bytes = 0, .tx_bytes = 19, .rx_packets = 28, .tx_packets = 0};
  service_->current_total_traffic_counters()
      [patchpanel::Client::TrafficSource::kVpn] = kVPNCounters;
  EXPECT_TRUE(service_->Save(&storage));
  patchpanel::Client::TrafficVector kActualVPNCounters;
  storage.GetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kVpn,
                        Service::kStorageTrafficCounterRxBytesSuffix),
                    &kActualVPNCounters.rx_bytes);
  storage.GetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kVpn,
                        Service::kStorageTrafficCounterTxBytesSuffix),
                    &kActualVPNCounters.tx_bytes);
  storage.GetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kVpn,
                        Service::kStorageTrafficCounterRxPacketsSuffix),
                    &kActualVPNCounters.rx_packets);
  storage.GetUint64(storage_id_,
                    Service::GetCurrentTrafficCounterKey(
                        patchpanel::Client::TrafficSource::kVpn,
                        Service::kStorageTrafficCounterTxPacketsSuffix),
                    &kActualVPNCounters.tx_packets);
  EXPECT_EQ(kVPNCounters, kActualVPNCounters);
}

TEST_F(ServiceTest, Save) {
  FakeStore storage;
  service_->technology_ = Technology::kWiFi;
  service_->probe_url_hint_ =
      net_base::HttpUrl::CreateFromString("http://www.url.com");
  EXPECT_TRUE(service_->Save(&storage));

  std::string type;
  EXPECT_TRUE(storage.GetString(storage_id_, Service::kStorageType, &type));
  EXPECT_EQ(type, service_->GetTechnologyName());

  std::string probe_url_string;
  EXPECT_TRUE(storage.GetString(storage_id_, Service::kStorageProbeUrlHint,
                                &probe_url_string));
  EXPECT_EQ("http://www.url.com", probe_url_string);
}

TEST_F(ServiceTest, SaveEap) {
  FakeStore storage;
  const char kIdentity[] = "identity";
  auto* eap = new EapCredentials();
  eap->set_identity(kIdentity);
  ASSERT_FALSE(service2_->eap());
  service2_->SetEapCredentials(eap);
  service2_->set_save_credentials(true);
  EXPECT_TRUE(service2_->Save(&storage));

  std::string identity;
  EXPECT_TRUE(storage.GetString(
      storage_id_, EapCredentials::kStorageCredentialEapIdentity, &identity));
  EXPECT_EQ(identity, kIdentity);
}

TEST_F(ServiceTest, RetainAutoConnect) {
  FakeStore storage;

  // AutoConnect flag set true.
  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->Save(&storage));
  bool auto_connect = false;
  EXPECT_TRUE(storage.GetBool(storage_id_, Service::kStorageAutoConnect,
                              &auto_connect));
  EXPECT_TRUE(auto_connect);

  // AutoConnect flag set false.
  service_->SetAutoConnect(false);
  EXPECT_TRUE(service_->Save(&storage));
  EXPECT_TRUE(storage.GetBool(storage_id_, Service::kStorageAutoConnect,
                              &auto_connect));
  EXPECT_FALSE(auto_connect);
}

TEST_F(ServiceTest, HasEverConnectedSavedToProfile) {
  FakeStore storage;

  // HasEverConnected flag set true.
  service_->SetHasEverConnected(true);
  EXPECT_TRUE(service_->Save(&storage));
  bool has_ever_connected = false;
  EXPECT_TRUE(storage.GetBool(storage_id_, Service::kStorageHasEverConnected,
                              &has_ever_connected));
  EXPECT_TRUE(has_ever_connected);

  // HasEverConnected flag set false.
  service_->SetHasEverConnected(false);
  EXPECT_TRUE(service_->Save(&storage));
  EXPECT_TRUE(storage.GetBool(storage_id_, Service::kStorageHasEverConnected,
                              &has_ever_connected));
  EXPECT_FALSE(has_ever_connected);
}

TEST_F(ServiceTest, Unload) {
  FakeStore storage;
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);
  storage.SetBool(storage_id_, Service::kStorageHasEverConnected, true);

  EXPECT_FALSE(service_->explicitly_disconnected_);
  EXPECT_FALSE(service_->has_ever_connected_);
  service_->explicitly_disconnected_ = true;

  EXPECT_TRUE(service_->Load(&storage));

  EXPECT_EQ(kGUID, service_->guid_);
  EXPECT_FALSE(service_->explicitly_disconnected_);
  EXPECT_TRUE(service_->has_ever_connected_);

  service_->explicitly_disconnected_ = true;
  SetStateField(Service::kStateConnected);
  service_->Unload();
  EXPECT_EQ(std::string(""), service_->guid_);
  EXPECT_FALSE(service_->explicitly_disconnected_);
  EXPECT_FALSE(service_->has_ever_connected_);
  EXPECT_EQ(1, service_->disconnect_calls());
}

TEST_F(ServiceTest, SaveAndLoadConnectionTimestamps) {
  FakeStore storage;
  base::Time time1, time2, time3;
  ASSERT_TRUE(base::Time::FromString("01 Jan 2018 12:00:00", &time1));
  ASSERT_TRUE(base::Time::FromString("02 Jan 2018 12:00:00", &time2));
  ASSERT_TRUE(base::Time::FromString("03 Jan 2018 12:00:00", &time3));
  service_->SetLastConnectedProperty(time1);
  service_->SetLastManualConnectAttemptProperty(time2);
  service_->SetLastOnlineProperty(time3);
  EXPECT_TRUE(service_->Save(&storage));

  // Verify the values in the storage
  uint64_t time_ms;
  EXPECT_TRUE(
      storage.GetUint64(storage_id_, Service::kStorageLastConnected, &time_ms));
  EXPECT_EQ(time_ms, time1.ToDeltaSinceWindowsEpoch().InMilliseconds());

  EXPECT_TRUE(storage.GetUint64(
      storage_id_, Service::kStorageLastManualConnectAttempt, &time_ms));
  EXPECT_EQ(time_ms, time2.ToDeltaSinceWindowsEpoch().InMilliseconds());

  EXPECT_TRUE(
      storage.GetUint64(storage_id_, Service::kStorageLastOnline, &time_ms));
  EXPECT_EQ(time_ms, time3.ToDeltaSinceWindowsEpoch().InMilliseconds());

  // Load into a separate service
  EXPECT_TRUE(service2_->Load(&storage));
  EXPECT_EQ(time1, base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(
                       service2_->GetLastConnectedProperty(nullptr))));
  EXPECT_EQ(time2,
            base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(
                service2_->GetLastManualConnectAttemptProperty(nullptr))));
  EXPECT_EQ(time3, base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(
                       service2_->GetLastOnlineProperty(nullptr))));
}

// Tests that callback is invoked properly when static IP configs changed.
TEST_F(ServiceTest, StaticIPConfigsChanged) {
  constexpr char kTestIpAddress1[] = "1.2.3.4";
  constexpr char kTestIpAddress2[] = "1.2.3.5";
  constexpr int kTestPrefix = 24;
  KeyValueStore static_ip_configs;
  FakeStore storage;

  const auto update_address = [&](const std::string& ip_addr) {
    static_ip_configs.Set(kAddressProperty, ip_addr);
    static_ip_configs.Set(kPrefixlenProperty, kTestPrefix);
    service_->mutable_store()->SetKeyValueStoreProperty(
        kStaticIPConfigProperty, static_ip_configs, /*error=*/nullptr);
  };
  update_address(kTestIpAddress1);
  ASSERT_TRUE(service_->Save(&storage));

  EXPECT_EQ(0, service_->GetNetworkID(nullptr));

  int fake_network_id = 335;
  EXPECT_CALL(*GetAdaptor(),
              EmitIntChanged(kNetworkIDProperty, fake_network_id));
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  ON_CALL(*network, network_id).WillByDefault(Return(fake_network_id));
  service_->AttachNetwork(network->AsWeakPtr());
  SetStateField(Service::kStateConnected);

  // Changes the address, network should be notified.
  EXPECT_CALL(*network, OnStaticIPConfigChanged);
  update_address(kTestIpAddress2);
  // Address is not changed, network should not be notified.
  update_address(kTestIpAddress2);
  // Reloads the service, network should be notified since address is changed.
  EXPECT_CALL(*network, OnStaticIPConfigChanged);
  ASSERT_TRUE(service_->Load(&storage));

  EXPECT_EQ(fake_network_id, service_->GetNetworkID(nullptr));

  // Persists the service and reloads again, network should not be notified
  // since address is not changed.
  ASSERT_TRUE(service_->Save(&storage));
  // Detaches the network, it should be notified once, but not any more.
  EXPECT_CALL(*network, OnStaticIPConfigChanged);
  EXPECT_CALL(*GetAdaptor(), EmitIntChanged(kNetworkIDProperty, 0));
  service_->DetachNetwork();
  update_address(kTestIpAddress2);

  EXPECT_EQ(0, service_->GetNetworkID(nullptr));
}

TEST_F(ServiceTest, State) {
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  EXPECT_EQ(Service::kFailureNone, service_->failure());
  const std::string no_error(
      Service::ConnectFailureToString(Service::kFailureNone));
  EXPECT_EQ(no_error, service_->error());

  EXPECT_CALL(*GetAdaptor(), EmitStringChanged(kStateProperty, _)).Times(6);
  EXPECT_CALL(*GetAdaptor(), EmitStringChanged(kErrorProperty, _)).Times(4);
  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  // A second state change shouldn't cause another update
  service_->SetState(Service::kStateConnected);
  EXPECT_EQ(Service::kStateConnected, service_->state());
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  EXPECT_EQ(Service::kFailureNone, service_->failure());
  EXPECT_TRUE(service_->has_ever_connected_);

  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  service_->SetFailure(Service::kFailureOutOfRange);
  EXPECT_TRUE(service_->IsFailed());
  std::optional<base::TimeDelta> time_failed = GetTimeSinceFailed();
  ASSERT_TRUE(time_failed);
  EXPECT_GT(*time_failed, base::TimeDelta());
  EXPECT_GT(service_->previous_error_serial_number_, 0);
  EXPECT_EQ(Service::kStateFailure, service_->state());
  EXPECT_EQ(Service::kFailureOutOfRange, service_->failure());
  const std::string out_of_range_error(
      Service::ConnectFailureToString(Service::kFailureOutOfRange));
  EXPECT_EQ(out_of_range_error, service_->error());
  EXPECT_EQ(out_of_range_error, service_->previous_error_);

  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_FALSE(service_->IsFailed());
  EXPECT_FALSE(GetTimeSinceFailed());
  EXPECT_EQ(no_error, service_->error());
  EXPECT_EQ(out_of_range_error, service_->previous_error_);
  EXPECT_GT(service_->previous_error_serial_number_, 0);

  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  service_->SetFailureSilent(Service::kFailurePinMissing);
  EXPECT_TRUE(service_->IsFailed());
  time_failed = GetTimeSinceFailed();
  ASSERT_TRUE(time_failed);
  EXPECT_GT(*time_failed, base::TimeDelta());
  EXPECT_GT(service_->previous_error_serial_number_, 0);
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ(Service::kFailurePinMissing, service_->failure());
  const std::string pin_missing_error(
      Service::ConnectFailureToString(Service::kFailurePinMissing));
  EXPECT_EQ(pin_missing_error, service_->error());
  EXPECT_EQ(pin_missing_error, service_->previous_error_);

  // If the Service has a Profile, the profile should be saved when
  // the service enters kStateConnected. (The case where the service
  // doesn't have a profile is tested above.)
  MockProfileRefPtr mock_profile(new MockProfile(&mock_manager_));
  FakeStore storage;
  service_->set_profile(mock_profile);
  service_->has_ever_connected_ = false;
  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(&storage));
  EXPECT_CALL(*mock_profile, UpdateService(Pointer(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_TRUE(service_->has_ever_connected_);
  service_->set_profile(nullptr);  // Break reference cycle.

  // Similar to the above, but emulate an emphemeral profile, which
  // has no storage. We can't update the service in the profile, but
  // we should not crash.
  service_->state_ = Service::kStateIdle;  // Skips state change logic.
  service_->set_profile(mock_profile);
  service_->has_ever_connected_ = false;
  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(nullptr));
  service_->SetState(Service::kStateConnected);
  EXPECT_TRUE(service_->has_ever_connected_);
  service_->set_profile(nullptr);  // Break reference cycle.
}

TEST_F(ServiceTest, StateResetAfterFailure) {
  service_->SetFailure(Service::kFailureOutOfRange);
  EXPECT_EQ(Service::kStateFailure, service_->state());
  Error error;
  service_->Connect(&error, "in test");
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ(Service::kFailureNone, service_->failure());

  service_->SetState(Service::kStateConnected);
  service_->Connect(&error, "in test");
  EXPECT_EQ(Service::kStateConnected, service_->state());
}

TEST_F(ServiceTest, UserInitiatedConnectionAttempt) {
  {
    // The initiation of connection attempt by user is successful.
    Error error;
    service_->SetState(Service::kStateIdle);
    service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
    EXPECT_TRUE(service_->is_in_user_connect());
  }
  {
    // The initiation of connection attempt by user fails, i.e. not attempted.
    // |Service::UserInitiatedConnect| checks if |error| is set during the call
    // stack through |Service::Connect| to know if the attempt is initiated
    // successfully. Here write a failure type in |error| to mimic this case.
    Error error(Error::kOperationFailed);
    service_->SetState(Service::kStateIdle);
    service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
    EXPECT_FALSE(service_->is_in_user_connect());
  }
}

TEST_F(ServiceTest, UserInitiatedConnectionResult) {
  service_->technology_ = Technology::kWiFi;
  Error error;
  // User-initiated connection attempt succeed.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult,
                            Metrics::kUserInitiatedConnectionResultSuccess));
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason, _))
      .Times(0);
  service_->SetState(Service::kStateConnected);
  Mock::VerifyAndClearExpectations(metrics());

  // User-initiated connection attempt failed.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult,
                            Metrics::kUserInitiatedConnectionResultFailure));
  EXPECT_CALL(
      *metrics(),
      SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionFailureReason,
                    Metrics::kUserInitiatedConnectionFailureReasonDHCP));
  service_->SetFailure(Service::kFailureDHCP);
  Mock::VerifyAndClearExpectations(metrics());

  // User-initiated connection attempt aborted.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  service_->SetState(Service::kStateAssociating);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult,
                            Metrics::kUserInitiatedConnectionResultAborted));
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason, _))
      .Times(0);
  service_->SetState(Service::kStateIdle);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for other state transition.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(
      *metrics(),
      SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult, _))
      .Times(0);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason, _))
      .Times(0);
  service_->SetState(Service::kStateAssociating);
  service_->SetState(Service::kStateConfiguring);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for non-user-initiated connection.
  service_->SetState(Service::kStateIdle);
  service_->Connect(&error, "in test");
  EXPECT_CALL(
      *metrics(),
      SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult, _))
      .Times(0);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason, _))
      .Times(0);
  service_->SetState(Service::kStateConnected);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for other technology.
  service_->technology_ = Technology::kCellular;
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(
      *metrics(),
      SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult, _))
      .Times(0);
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason, _))
      .Times(0);
  service_->SetFailure(Service::kFailureDHCP);
  Mock::VerifyAndClearExpectations(metrics());
}

TEST_F(ServiceTest, CompleteCellularActivation) {
  Error error;
  service_->CompleteCellularActivation(&error);
  EXPECT_EQ(Error::kNotImplemented, error.type());
}

TEST_F(ServiceTest, EnableAndRetainAutoConnect) {
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());

  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_TRUE(service_->auto_connect());
}

TEST_F(ServiceTest, ReRetainAutoConnect) {
  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_TRUE(service_->auto_connect());

  service_->SetAutoConnect(false);
  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());
}

TEST_F(ServiceTest, IsAutoConnectable) {
  const char* reason = nullptr;
  service_->SetConnectable(true);

  // Services with non-primary connectivity technologies should not auto-connect
  // when the system is offline.
  service_->technology_ = Technology::kUnknown;
  EXPECT_EQ(Technology::kUnknown, service_->technology());
  EXPECT_CALL(mock_manager_, IsConnected()).WillOnce(Return(false));
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnOffline, reason);

  service_->technology_ = Technology::kEthernet;
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // We should not auto-connect to a Service that a user has
  // deliberately disconnected.
  Error error;
  service_->UserInitiatedDisconnect(kConnectDisconnectReason, &error);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnExplicitDisconnect, reason);

  // But if the Service is reloaded, it is eligible for auto-connect
  // again.
  FakeStore storage;
  storage.SetString(storage_id_, Service::kStorageGUID, kGUID);
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // A deliberate Connect should also re-enable auto-connect.
  service_->UserInitiatedDisconnect(kConnectDisconnectReason, &error);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  service_->Connect(&error, "in test");
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // A non-user initiated Disconnect doesn't change anything.
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // A resume also re-enables auto-connect.
  service_->UserInitiatedDisconnect(kConnectDisconnectReason, &error);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  service_->OnAfterResume();
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  service_->SetState(Service::kStateConnected);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnConnected, reason);

  service_->SetState(Service::kStateAssociating);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnConnecting, reason);

  service_->SetState(Service::kStateIdle);
  EXPECT_CALL(mock_manager_,
              IsTechnologyAutoConnectDisabled(service_->technology_))
      .WillOnce(Return(true));
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnTechnologyNotAutoConnectable, reason);
}

TEST_F(AllMockServiceTest, AutoConnectWithFailures) {
  const char* reason;
  service_->SetConnectable(true);
  service_->SetAutoConnect(true);
  service_->technology_ = Technology::kEthernet;
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // The very first AutoConnect() doesn't trigger any throttling.
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, _)).Times(0);
  service_->AutoConnect();
  Mock::VerifyAndClearExpectations(&dispatcher_);
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // The second call does trigger some throttling.
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, _, service_->GetMinAutoConnectCooldownTime()));
  service_->AutoConnect();
  Mock::VerifyAndClearExpectations(&dispatcher_);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnThrottled, reason);

  // Calling AutoConnect() again before the cooldown terminates does not change
  // the timeout.
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, _)).Times(0);
  service_->AutoConnect();
  Mock::VerifyAndClearExpectations(&dispatcher_);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnThrottled, reason);

  // Once the timeout expires, we can AutoConnect() again.
  service_->ReEnableAutoConnectTask();
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // Timeouts increase exponentially.
  base::TimeDelta next_cooldown_time = service_->auto_connect_cooldown_;
  EXPECT_EQ(next_cooldown_time, Service::kAutoConnectCooldownBackoffFactor *
                                    service_->GetMinAutoConnectCooldownTime());
  while (next_cooldown_time <= service_->GetMaxAutoConnectCooldownTime()) {
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, next_cooldown_time));
    service_->AutoConnect();
    Mock::VerifyAndClearExpectations(&dispatcher_);
    EXPECT_FALSE(service_->IsAutoConnectable(&reason));
    EXPECT_STREQ(Service::kAutoConnThrottled, reason);
    service_->ReEnableAutoConnectTask();
    next_cooldown_time *= Service::kAutoConnectCooldownBackoffFactor;
  }

  // Once we hit our cap, future timeouts are the same.
  for (int32_t i = 0; i < 2; i++) {
    EXPECT_CALL(
        dispatcher_,
        PostDelayedTask(_, _, service_->GetMaxAutoConnectCooldownTime()));
    service_->AutoConnect();
    Mock::VerifyAndClearExpectations(&dispatcher_);
    EXPECT_FALSE(service_->IsAutoConnectable(&reason));
    EXPECT_STREQ(Service::kAutoConnThrottled, reason);
    service_->ReEnableAutoConnectTask();
  }

  // Connecting successfully resets our cooldown.
  service_->SetState(Service::kStateConnected);
  service_->SetState(Service::kStateIdle);
  reason = "";
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ("", reason);
  EXPECT_TRUE(service_->auto_connect_cooldown_.is_zero());

  // But future AutoConnects behave as before
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, _, service_->GetMinAutoConnectCooldownTime()))
      .Times(1);
  service_->AutoConnect();
  service_->AutoConnect();
  Mock::VerifyAndClearExpectations(&dispatcher_);
  EXPECT_FALSE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnThrottled, reason);

  // Cooldowns are forgotten if we go through a suspend/resume cycle.
  service_->OnAfterResume();
  reason = "";
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));
  EXPECT_STREQ("", reason);
}

TEST_F(ServiceTest, SkipAutoConnectAfterRecentBadPassphraseFailure) {
  const char* reason;
  service_->SetConnectable(true);
  service_->SetAutoConnect(true);
  SetTechnology(Technology::kWiFi);
  EXPECT_TRUE(IsAutoConnectable(&reason));

  service_->set_failed_time_for_testing(base::Time::Now());
  service_->set_previous_error_for_testing(kErrorBadPassphrase);
  service_->AutoConnect();
  EXPECT_EQ(0, service_->connect_calls());
}

TEST_F(ServiceTest, ConfigureBadProperty) {
  KeyValueStore args;
  args.Set<std::string>("XXXInvalid", "Value");
  Error error;
  service_->Configure(args, &error);
  EXPECT_FALSE(error.IsSuccess());
}

TEST_F(ServiceTest, ConfigureBoolProperty) {
  service_->EnableAndRetainAutoConnect();
  service_->SetAutoConnect(false);
  ASSERT_FALSE(service_->auto_connect());
  KeyValueStore args;
  args.Set<bool>(kAutoConnectProperty, true);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(service_->auto_connect());
}

TEST_F(ServiceTest, ConfigureStringProperty) {
  const std::string kGuid0 = "guid_zero";
  const std::string kGuid1 = "guid_one";
  service_->SetGuid(kGuid0, nullptr);
  ASSERT_EQ(kGuid0, service_->guid());
  KeyValueStore args;
  args.Set<std::string>(kGuidProperty, kGuid1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kGuid1, service_->guid());
}

TEST_F(ServiceTest, ConfigureStringsProperty) {
  const std::vector<std::string> kStrings0{"string0", "string1"};
  const std::vector<std::string> kStrings1{"string2", "string3"};
  service_->set_strings(kStrings0);
  ASSERT_EQ(kStrings0, service_->strings());
  KeyValueStore args;
  args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kStrings1, service_->strings());
}

TEST_F(ServiceTest, ConfigureEapStringProperty) {
  MockEapCredentials* eap = new NiceMock<MockEapCredentials>();
  service2_->SetEapCredentials(eap);  // Passes ownership.

  const std::string kEAPManagement0 = "management_zero";
  const std::string kEAPManagement1 = "management_one";
  service2_->SetEAPKeyManagement(kEAPManagement0);

  EXPECT_CALL(*eap, key_management()).WillOnce(ReturnRef(kEAPManagement0));
  ASSERT_EQ(kEAPManagement0, service2_->GetEAPKeyManagement());
  KeyValueStore args;
  EXPECT_CALL(*eap, SetKeyManagement(kEAPManagement1, _));
  args.Set<std::string>(kEapKeyMgmtProperty, kEAPManagement1);
  Error error;
  service2_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, ConfigureIntProperty) {
  const int kPriority0 = 100;
  const int kPriority1 = 200;
  service_->SetPriority(kPriority0, nullptr);
  ASSERT_EQ(kPriority0, service_->priority());
  KeyValueStore args;
  args.Set<int32_t>(kPriorityProperty, kPriority1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kPriority1, service_->priority());
}

TEST_F(ServiceTest, ConfigureIgnoredProperty) {
  service_->EnableAndRetainAutoConnect();
  service_->SetAutoConnect(false);
  ASSERT_FALSE(service_->auto_connect());
  KeyValueStore args;
  args.Set<bool>(kAutoConnectProperty, true);
  Error error;
  service_->IgnoreParameterForConfigure(kAutoConnectProperty);
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->auto_connect());
}

TEST_F(ServiceTest, ConfigureProfileProperty) {
  // Ensure that the Profile property is always ignored.
  KeyValueStore args;
  args.Set<std::string>(kProfileProperty, "profile");
  Error error;
  EXPECT_CALL(mock_manager_, SetProfileForService(_, _, _)).Times(0);
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, ConfigureKeyValueStoreProperty) {
  KeyValueStore key_value_store0;
  key_value_store0.Set<bool>("key0", true);
  KeyValueStore key_value_store1;
  key_value_store1.Set<int32_t>("key1", 1);
  service_->SetKeyValueStore(key_value_store0, nullptr);
  ASSERT_EQ(key_value_store0, service_->GetKeyValueStore(nullptr));
  KeyValueStore args;
  args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(key_value_store1, service_->GetKeyValueStore(nullptr));
}

TEST_F(ServiceTest, DoPropertiesMatch) {
  service_->SetAutoConnect(false);
  const std::string kGUID0 = "guid_zero";
  const std::string kGUID1 = "guid_one";
  service_->SetGuid(kGUID0, nullptr);
  const uint32_t kPriority0 = 100;
  const uint32_t kPriority1 = 200;
  service_->SetPriority(kPriority0, nullptr);
  const std::vector<std::string> kStrings0{"string0", "string1"};
  const std::vector<std::string> kStrings1{"string2", "string3"};
  service_->set_strings(kStrings0);
  KeyValueStore key_value_store0;
  key_value_store0.Set<bool>("key0", true);
  KeyValueStore key_value_store1;
  key_value_store1.Set<int32_t>("key1", 1);
  service_->SetKeyValueStore(key_value_store0, nullptr);

  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID0);
    args.Set<bool>(kAutoConnectProperty, false);
    args.Set<int32_t>(kPriorityProperty, kPriority0);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings0);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store0);
    EXPECT_TRUE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID1);
    args.Set<bool>(kAutoConnectProperty, false);
    args.Set<int32_t>(kPriorityProperty, kPriority0);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings0);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID0);
    args.Set<bool>(kAutoConnectProperty, true);
    args.Set<int32_t>(kPriorityProperty, kPriority0);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings0);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID0);
    args.Set<bool>(kAutoConnectProperty, false);
    args.Set<int32_t>(kPriorityProperty, kPriority1);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings0);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID0);
    args.Set<bool>(kAutoConnectProperty, false);
    args.Set<int32_t>(kPriorityProperty, kPriority0);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings1);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.Set<std::string>(kGuidProperty, kGUID0);
    args.Set<bool>(kAutoConnectProperty, false);
    args.Set<int32_t>(kPriorityProperty, kPriority0);
    args.Set<Strings>(ServiceUnderTest::kStringsProperty, kStrings0);
    args.Set<KeyValueStore>(ServiceUnderTest::kKeyValueStoreProperty,
                            key_value_store1);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
}

TEST_F(ServiceTest, IsRemembered) {
  service_->set_profile(nullptr);
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(_)).Times(0);
  EXPECT_FALSE(service_->IsRemembered());

  scoped_refptr<MockProfile> profile(new StrictMock<MockProfile>(manager()));
  service_->set_profile(profile);
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(Pointer(service_)))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_FALSE(service_->IsRemembered());
  EXPECT_TRUE(service_->IsRemembered());
}

TEST_F(ServiceTest, OnPropertyChanged) {
  scoped_refptr<MockProfile> profile(new StrictMock<MockProfile>(manager()));
  service_->set_profile(nullptr);
  // Expect no crash.
  service_->OnPropertyChanged("");

  // Expect no call to Update if the profile has no storage.
  service_->set_profile(profile);
  EXPECT_CALL(*profile, UpdateService(_)).Times(0);
  EXPECT_CALL(*profile, GetConstStorage()).WillOnce(Return(nullptr));
  service_->OnPropertyChanged("");

  // Expect call to Update if the profile has storage.
  EXPECT_CALL(*profile, UpdateService(_)).Times(1);
  FakeStore storage;
  EXPECT_CALL(*profile, GetConstStorage()).WillOnce(Return(&storage));
  service_->OnPropertyChanged("");
}

TEST_F(ServiceTest, SetCheckPortal) {
  auto network =
      std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());

  // Ensure the network validation mode is set to full validation.
  EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
      .WillRepeatedly(Return(true));
  {
    Error error;
    service_->SetProxyConfig("", &error);
  }
  SetStateField(Service::kStateConnected);
  EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
            service_->GetNetworkValidationMode());

  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kDisabled));
    Error error;
    service_->SetCheckPortal("false", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::CheckPortalState::kFalse, service_->check_portal());
    EXPECT_EQ("false", service_->GetCheckPortal(nullptr));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kFullValidation));
    Error error;
    service_->SetCheckPortal("auto", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::CheckPortalState::kAutomatic, service_->check_portal());
    EXPECT_EQ("auto", service_->GetCheckPortal(nullptr));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kFullValidation));
    Error error;
    service_->SetCheckPortal("true", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::CheckPortalState::kTrue, service_->check_portal());
    EXPECT_EQ("true", service_->GetCheckPortal(nullptr));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode).Times(0);
    Error error;
    service_->SetCheckPortal("xxx", &error);
    EXPECT_FALSE(error.IsSuccess());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
    EXPECT_EQ(Service::CheckPortalState::kTrue, service_->check_portal());
    EXPECT_EQ("true", service_->GetCheckPortal(nullptr));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
}

TEST_F(ServiceTest, SetProxyConfig) {
  auto network =
      std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());

  EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
      .WillRepeatedly(Return(true));
  {
    Error error;
    service_->SetCheckPortal("auto", &error);
  }
  SetStateField(Service::kStateConnected);
  EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
            service_->GetNetworkValidationMode());

  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kHTTPOnly));
    Error error;
    service_->SetProxyConfig("{\"mode\":\"auto_detect\"}", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_TRUE(service_->HasProxyConfig());
    EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kFullValidation));
    Error error;
    service_->SetProxyConfig("{\"mode\":\"direct\"}", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_FALSE(service_->HasProxyConfig());
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kFullValidation));
    Error error;
    service_->SetProxyConfig("", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_FALSE(service_->HasProxyConfig());
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
  {
    // If network validation is disabled for a certain technology, no matter
    // what the proxy config setting is, it should be disabled for this service.
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*network, UpdateNetworkValidationMode(
                              NetworkMonitor::ValidationMode::kDisabled));
    Error error;
    service_->SetProxyConfig("{\"mode\":\"auto_detect\"}", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_TRUE(service_->HasProxyConfig());
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
    Mock::VerifyAndClearExpectations(network.get());
  }
}

TEST_F(ServiceTest, NetworkValidationMode) {
  {
    // The service has a proxy configuration == "direct", but "CheckPortal" is
    // "true".
    Error error;
    service_->SetCheckPortal("true", &error);
    service_->SetProxyConfig("{\"mode\":\"direct\"}", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service has a proxy configuration != "direct", but "CheckPortal" is
    // "true".
    Error error;
    service_->SetCheckPortal("true", &error);
    service_->SetProxyConfig("{\"mode\":\"auto_detect\"}", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service is configured through ONC device policy, but "CheckPortal" is
    // "true".
    Error error;
    service_->SetCheckPortal("true", &error);
    service_->SetONCSource("DevicePolicy", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service is configured through ONC user policy, but "CheckPortal" is
    // "true".
    Error error;
    service_->SetCheckPortal("true", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("UserPolicy", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "false".
    Error error;
    service_->SetCheckPortal("false", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and portal
    // detection is disabled for this link technology.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(false));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and the service is
    // configured through ONC device policy.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("DevicePolicy", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and the service is
    // configured through ONC user policy.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("UserPolicy", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and portal
    // detection is enabled for this link technology.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and the service
    // direct proxy configuration is ignored.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("{\"mode\":\"direct\"}", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "auto" and the service
    // has a per-network proxy configuration.
    Error error;
    service_->SetCheckPortal("auto", &error);
    service_->SetProxyConfig("{\"mode\":\"auto_detect\"}", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "http-only" and portal
    // detection is enabled for this link technology.
    Error error;
    service_->SetCheckPortal("http-only", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "http-only" and portal
    // detection is disabled for this link technology.
    Error error;
    service_->SetCheckPortal("http-only", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("None", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(false));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
              service_->GetNetworkValidationMode());
  }
  {
    // The service's "CheckPortal" property is set to "http-only" and the
    // service is configured through ONC device policy.
    Error error;
    service_->SetCheckPortal("http-only", &error);
    service_->SetProxyConfig("", &error);
    service_->SetONCSource("DevicePolicy", &error);
    EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled)
        .WillRepeatedly(Return(true));
    EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
              service_->GetNetworkValidationMode());
  }
}

TEST_F(ServiceTest, SetFriendlyName) {
  std::string default_friendly_name = service_->friendly_name_;
  ServiceMockAdaptor* adaptor = GetAdaptor();

  EXPECT_CALL(*adaptor, EmitStringChanged(_, _)).Times(0);
  service_->SetFriendlyName(default_friendly_name);
  EXPECT_EQ(default_friendly_name, service_->friendly_name_);

  EXPECT_CALL(*adaptor, EmitStringChanged(kNameProperty, "Test Name 1"));
  service_->SetFriendlyName("Test Name 1");
  EXPECT_EQ("Test Name 1", service_->friendly_name_);

  EXPECT_CALL(*adaptor, EmitStringChanged(_, _)).Times(0);
  service_->SetFriendlyName("Test Name 1");
  EXPECT_EQ("Test Name 1", service_->friendly_name_);

  EXPECT_CALL(*adaptor, EmitStringChanged(kNameProperty, "Test Name 2"));
  service_->SetFriendlyName("Test Name 2");
  EXPECT_EQ("Test Name 2", service_->friendly_name_);
}

TEST_F(ServiceTest, SetConnectableFull) {
  EXPECT_TRUE(service_->connectable());

  ServiceMockAdaptor* adaptor = GetAdaptor();

  EXPECT_CALL(*adaptor, EmitBoolChanged(_, _)).Times(0);
  EXPECT_CALL(mock_manager_, HasService(_)).Times(0);
  service_->SetConnectableFull(true);
  EXPECT_TRUE(service_->connectable());

  EXPECT_CALL(*adaptor, EmitBoolChanged(kConnectableProperty, false));
  EXPECT_CALL(mock_manager_, HasService(_)).WillOnce(Return(true));
  EXPECT_CALL(mock_manager_, UpdateService(_));
  service_->SetConnectableFull(false);
  EXPECT_FALSE(service_->connectable());

  EXPECT_CALL(*adaptor, EmitBoolChanged(_, _)).Times(0);
  EXPECT_CALL(mock_manager_, HasService(_)).Times(0);
  service_->SetConnectableFull(false);
  EXPECT_FALSE(service_->connectable());

  EXPECT_CALL(*adaptor, EmitBoolChanged(kConnectableProperty, true));
  EXPECT_CALL(mock_manager_, HasService(_)).WillOnce(Return(true));
  EXPECT_CALL(mock_manager_, UpdateService(_));
  service_->SetConnectableFull(true);
  EXPECT_TRUE(service_->connectable());
}

class WriteOnlyServicePropertyTest : public ServiceTest {};
TEST_P(WriteOnlyServicePropertyTest, PropertyWriteOnly) {
  // Use a real EapCredentials instance since the base Service class
  // contains no write-only properties.
  EapCredentials eap;
  eap.InitPropertyStore(service_->mutable_store());

  std::string property(GetParam().Get<std::string>());
  Error error;
  EXPECT_FALSE(service_->store().GetStringProperty(property, nullptr, &error));
  EXPECT_EQ(Error::kPermissionDenied, error.type());
}

INSTANTIATE_TEST_SUITE_P(
    WriteOnlyServicePropertyTestInstance,
    WriteOnlyServicePropertyTest,
    Values(brillo::Any(std::string(kEapPasswordProperty))));

TEST_F(ServiceTest, AttachedNetworkChangeTriggersEmitPropertyChanged) {
  {
    Error error;
    EXPECT_EQ(DBusControl::NullRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kNotFound, error.type());
    EXPECT_EQ(0, service_->GetNetworkID(nullptr));
  }

  {
    int fake_network_id = 335;
    auto ipconfig = std::make_unique<IPConfig>(control_interface(), kIfName);
    auto network =
        std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
    ON_CALL(*network, network_id).WillByDefault(Return(fake_network_id));
    ON_CALL(*network, GetCurrentIPConfig())
        .WillByDefault(Return(ipconfig.get()));

    EXPECT_CALL(*GetAdaptor(),
                EmitIntChanged(kNetworkIDProperty, fake_network_id));
    EXPECT_CALL(*GetAdaptor(),
                EmitRpcIdentifierChanged(kIPConfigProperty,
                                         ipconfig->GetRpcIdentifier()));
    service_->AttachNetwork(network->AsWeakPtr());
    EXPECT_EQ(fake_network_id, service_->GetNetworkID(nullptr));
    Error error;
    EXPECT_EQ(ipconfig->GetRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_TRUE(error.IsSuccess());

    EXPECT_CALL(*GetAdaptor(), EmitIntChanged(kNetworkIDProperty, 0));
    EXPECT_CALL(*GetAdaptor(), EmitRpcIdentifierChanged(kIPConfigProperty, _))
        .Times(0);
    service_->DetachNetwork();
  }

  {
    Error error;
    EXPECT_EQ(DBusControl::NullRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kNotFound, error.type());
    EXPECT_EQ(0, service_->GetNetworkID(nullptr));
  }

  {
    int fake_network_id = 744;
    auto network =
        std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
    ON_CALL(*network, network_id).WillByDefault(Return(fake_network_id));
    EXPECT_CALL(*GetAdaptor(),
                EmitIntChanged(kNetworkIDProperty, fake_network_id));
    EXPECT_CALL(*GetAdaptor(), EmitRpcIdentifierChanged(kIPConfigProperty, _))
        .Times(0);
    service_->AttachNetwork(network->AsWeakPtr());
    EXPECT_EQ(fake_network_id, service_->GetNetworkID(nullptr));

    Error error;
    EXPECT_EQ(DBusControl::NullRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }
}

class ServiceWithMockOnEapCredentialsChanged : public ServiceUnderTest {
 public:
  explicit ServiceWithMockOnEapCredentialsChanged(Manager* manager)
      : ServiceUnderTest(manager) {}
  MOCK_METHOD(void,
              OnEapCredentialsChanged,
              (Service::UpdateCredentialsReason),
              (override));
  bool Is8021x() const override { return is_8021x_; }
  void set_is_8021x(bool is_8021x) { is_8021x_ = is_8021x; }

 private:
  bool is_8021x_ = false;
};

TEST_F(ServiceTest, SetEAPCredentialsOverRpc) {
  scoped_refptr<ServiceWithMockOnEapCredentialsChanged> service(
      new ServiceWithMockOnEapCredentialsChanged(&mock_manager_));
  static const char* const kEapCredentialProperties[] = {
      kEapAnonymousIdentityProperty, kEapCertIdProperty,
      kEapIdentityProperty,          kEapKeyIdProperty,
      kEapPasswordProperty,          kEapPinProperty,
  };
  static const char* const kEapNonCredentialProperties[] = {
      kEapCaCertIdProperty, kEapMethodProperty, kEapPhase2AuthProperty,
      kEapUseSystemCasProperty};
  // While this is not an 802.1x-based service, none of these property
  // changes should cause a call to set_eap().
  EXPECT_CALL(*service, OnEapCredentialsChanged(_)).Times(0);
  for (auto eap_credential_property : kEapCredentialProperties) {
    service->OnPropertyChanged(eap_credential_property);
  }
  for (auto eap_non_credential_property : kEapNonCredentialProperties) {
    service->OnPropertyChanged(eap_non_credential_property);
  }
  service->OnPropertyChanged(kEapKeyMgmtProperty);

  service->set_is_8021x(true);

  // When this is an 802.1x-based service, set_eap should be called for
  // all credential-carrying properties.
  for (auto eap_credential_property : kEapCredentialProperties) {
    EXPECT_CALL(*service,
                OnEapCredentialsChanged(Service::kReasonPropertyUpdate))
        .Times(1);
    service->OnPropertyChanged(eap_credential_property);
    Mock::VerifyAndClearExpectations(service.get());
  }

  // The key management property is a special case.  While not strictly
  // a credential, it can change which credentials are used.  Therefore it
  // should also trigger a call to set_eap();
  EXPECT_CALL(*service, OnEapCredentialsChanged(Service::kReasonPropertyUpdate))
      .Times(1);
  service->OnPropertyChanged(kEapKeyMgmtProperty);
  Mock::VerifyAndClearExpectations(service.get());

  EXPECT_CALL(*service, OnEapCredentialsChanged(_)).Times(0);
  for (auto eap_non_credential_property : kEapNonCredentialProperties) {
    service->OnPropertyChanged(eap_non_credential_property);
  }
}

TEST_F(ServiceTest, Certification) {
  EXPECT_TRUE(service_->remote_certification_.empty());

  ScopedMockLog log;
  EXPECT_CALL(
      log, Log(logging::LOGGING_WARNING, _, HasSubstr("exceeds our maximum")))
      .Times(2);
  std::string kSubject("foo");
  EXPECT_FALSE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements));
  EXPECT_FALSE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements + 1));
  EXPECT_FALSE(service_->remote_certification_.size());
  Mock::VerifyAndClearExpectations(&log);

  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("Received certification")))
      .Times(1);
  EXPECT_TRUE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements - 1));
  Mock::VerifyAndClearExpectations(&log);
  EXPECT_EQ(Service::kEAPMaxCertificationElements,
            service_->remote_certification_.size());
  for (size_t i = 0; i < Service::kEAPMaxCertificationElements - 1; ++i) {
    EXPECT_TRUE(service_->remote_certification_[i].empty());
  }
  EXPECT_EQ(
      kSubject,
      service_
          ->remote_certification_[Service::kEAPMaxCertificationElements - 1]);

  // Re-adding the same name in the same position should not generate a log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_TRUE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements - 1));

  // Replacing the item should generate a log message.
  EXPECT_CALL(
      log, Log(logging::LOGGING_INFO, _, HasSubstr("Received certification")))
      .Times(1);
  EXPECT_TRUE(service_->AddEAPCertification(
      kSubject + "x", Service::kEAPMaxCertificationElements - 1));

  service_->ClearEAPCertification();
  EXPECT_TRUE(service_->remote_certification_.empty());
}

TEST_F(ServiceTest, NoteFailureEventIdle) {
  Timestamp timestamp;
  EXPECT_CALL(time_, GetNow()).Times(7).WillRepeatedly((Return(timestamp)));
  SetStateField(Service::kStateOnline);
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  service_->SetState(Service::kStateIdle);
  // The transition Online->Idle is not an event.
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  service_->SetState(Service::kStateFailure);
  // The transition Online->Idle->Failure is a connection drop.
  EXPECT_TRUE(service_->HasRecentConnectionIssues());
}

TEST_F(ServiceTest, NoteFailureEventOnSetStateFailure) {
  Timestamp timestamp;
  EXPECT_CALL(time_, GetNow()).Times(5).WillRepeatedly((Return(timestamp)));
  SetStateField(Service::kStateOnline);
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  service_->SetState(Service::kStateFailure);
  EXPECT_TRUE(service_->HasRecentConnectionIssues());
}

TEST_F(ServiceTest, NoteFailureEventOnSetFailureSilent) {
  Timestamp timestamp;
  EXPECT_CALL(time_, GetNow()).Times(5).WillRepeatedly((Return(timestamp)));
  SetStateField(Service::kStateConfiguring);
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  service_->SetFailureSilent(Service::kFailureEAPAuthentication);
  EXPECT_TRUE(service_->HasRecentConnectionIssues());
}

TEST_F(ServiceTest, NoteFailureEventNonEvent) {
  EXPECT_CALL(time_, GetNow()).Times(0);

  // Explicit disconnect is a non-event.
  SetStateField(Service::kStateOnline);
  SetExplicitlyDisconnected(true);
  NoteFailureEvent();
  EXPECT_TRUE(GetDisconnects()->Empty());
  EXPECT_TRUE(GetMisconnects()->Empty());

  // Failure to idle transition is a non-event.
  SetStateField(Service::kStateFailure);
  SetExplicitlyDisconnected(false);
  NoteFailureEvent();
  EXPECT_TRUE(GetDisconnects()->Empty());
  EXPECT_TRUE(GetMisconnects()->Empty());

  // Disconnect while manager is stopped is a non-event.
  SetStateField(Service::kStateOnline);
  SetManagerRunning(false);
  NoteFailureEvent();
  EXPECT_TRUE(GetDisconnects()->Empty());
  EXPECT_TRUE(GetMisconnects()->Empty());

  // Disconnect while suspending is a non-event.
  SetManagerRunning(true);
  SetSuspending(true);
  NoteFailureEvent();
  EXPECT_TRUE(GetDisconnects()->Empty());
  EXPECT_TRUE(GetMisconnects()->Empty());
}

TEST_F(ServiceTest, NoteFailureEventDisconnectOnce) {
  const int kNow = 5;
  EXPECT_FALSE(service_->explicitly_disconnected());
  SetStateField(Service::kStateOnline);
  EXPECT_CALL(time_, GetNow()).WillOnce(Return(GetTimestamp(kNow, kNow, "")));
  NoteFailureEvent();
  ASSERT_EQ(1, GetDisconnects()->Size());
  EXPECT_EQ(kNow, GetDisconnects()->Front().monotonic.tv_sec);
  EXPECT_TRUE(GetMisconnects()->Empty());

  Mock::VerifyAndClearExpectations(&time_);
  EXPECT_CALL(time_, GetNow())
      .Times(2)
      .WillRepeatedly(
          Return(GetTimestamp(kNow + GetDisconnectsMonitorSeconds() - 1,
                              kNow + GetDisconnectsMonitorSeconds() - 1, "")));
  EXPECT_TRUE(service_->HasRecentConnectionIssues());
  ASSERT_EQ(1, GetDisconnects()->Size());

  Mock::VerifyAndClearExpectations(&time_);
  EXPECT_CALL(time_, GetNow())
      .Times(2)
      .WillRepeatedly(
          Return(GetTimestamp(kNow + GetDisconnectsMonitorSeconds(),
                              kNow + GetDisconnectsMonitorSeconds(), "")));
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  ASSERT_TRUE(GetDisconnects()->Empty());
}

TEST_F(ServiceTest, NoteFailureEventMisconnectOnce) {
  const int kNow = 7;
  EXPECT_FALSE(service_->explicitly_disconnected());
  SetStateField(Service::kStateConfiguring);
  EXPECT_CALL(time_, GetNow()).WillOnce(Return(GetTimestamp(kNow, kNow, "")));
  NoteFailureEvent();
  EXPECT_TRUE(GetDisconnects()->Empty());
  ASSERT_EQ(1, GetMisconnects()->Size());
  EXPECT_EQ(kNow, GetMisconnects()->Front().monotonic.tv_sec);

  Mock::VerifyAndClearExpectations(&time_);
  EXPECT_CALL(time_, GetNow())
      .Times(2)
      .WillRepeatedly(
          Return(GetTimestamp(kNow + GetMisconnectsMonitorSeconds() - 1,
                              kNow + GetMisconnectsMonitorSeconds() - 1, "")));
  EXPECT_TRUE(service_->HasRecentConnectionIssues());
  ASSERT_EQ(1, GetMisconnects()->Size());

  Mock::VerifyAndClearExpectations(&time_);
  EXPECT_CALL(time_, GetNow())
      .Times(2)
      .WillRepeatedly(
          Return(GetTimestamp(kNow + GetMisconnectsMonitorSeconds(),
                              kNow + GetMisconnectsMonitorSeconds(), "")));
  EXPECT_FALSE(service_->HasRecentConnectionIssues());
  ASSERT_TRUE(GetMisconnects()->Empty());
}

TEST_F(ServiceTest, NoteFailureEventDiscardOld) {
  EXPECT_FALSE(service_->explicitly_disconnected());
  for (int i = 0; i < 2; i++) {
    int now = 0;
    EventHistory* events = nullptr;
    if (i == 0) {
      SetStateField(Service::kStateConnected);
      now = GetDisconnectsMonitorSeconds() + 1;
      events = GetDisconnects();
    } else {
      SetStateField(Service::kStateAssociating);
      now = GetMisconnectsMonitorSeconds() + 1;
      events = GetMisconnects();
    }
    PushTimestamp(events, 0, 0, "");
    PushTimestamp(events, 0, 0, "");
    EXPECT_CALL(time_, GetNow()).WillOnce(Return(GetTimestamp(now, now, "")));
    NoteFailureEvent();
    ASSERT_EQ(1, events->Size());
    EXPECT_EQ(now, events->Front().monotonic.tv_sec);
  }
}

TEST_F(ServiceTest, NoteFailureEventDiscardExcessive) {
  EXPECT_FALSE(service_->explicitly_disconnected());
  SetStateField(Service::kStateOnline);
  for (int i = 0; i < 2 * GetMaxDisconnectEventHistory(); i++) {
    PushTimestamp(GetDisconnects(), 0, 0, "");
  }
  EXPECT_CALL(time_, GetNow()).WillOnce(Return(Timestamp()));
  NoteFailureEvent();
  EXPECT_EQ(GetMaxDisconnectEventHistory(), GetDisconnects()->Size());
}

TEST_F(ServiceTest, NoteMisconnectEventDiscardExcessive) {
  EXPECT_FALSE(service_->explicitly_disconnected());
  SetStateField(Service::kStateAssociating);
  for (int i = 0; i < 2 * GetMaxMisconnectEventHistory(); i++) {
    PushTimestamp(GetMisconnects(), 0, 0, "");
  }
  EXPECT_CALL(time_, GetNow()).WillOnce(Return(Timestamp()));
  NoteFailureEvent();
  EXPECT_EQ(GetMaxMisconnectEventHistory(), GetMisconnects()->Size());
}

TEST_F(ServiceTest, DiagnosticsProperties) {
  const std::string kWallClock0 = "2012-12-09T12:41:22.234567-0800";
  const std::string kWallClock1 = "2012-12-31T23:59:59.345678-0800";
  Strings values;

  PushTimestamp(GetDisconnects(), 0, 0, kWallClock0);
  Error unused_error;
  ASSERT_TRUE(service_->store().GetStringsProperty(
      kDiagnosticsDisconnectsProperty, &values, &unused_error));
  ASSERT_EQ(1, values.size());
  EXPECT_EQ(kWallClock0, values[0]);

  PushTimestamp(GetMisconnects(), 0, 0, kWallClock1);
  ASSERT_TRUE(service_->store().GetStringsProperty(
      kDiagnosticsMisconnectsProperty, &values, &unused_error));
  ASSERT_EQ(1, values.size());
  EXPECT_EQ(kWallClock1, values[0]);
}

TEST_F(ServiceTest, SecurityLevel) {
  // Encrypted is better than not.
  service_->SetSecurity(Service::kCryptoNone, false, false);
  service2_->SetSecurity(Service::kCryptoRc4, false, false);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());

  // AES encryption is better than RC4 encryption.
  service_->SetSecurity(Service::kCryptoRc4, false, false);
  service2_->SetSecurity(Service::kCryptoAes, false, false);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());

  // Crypto algorithm is more important than key rotation.
  service_->SetSecurity(Service::kCryptoNone, true, false);
  service2_->SetSecurity(Service::kCryptoAes, false, false);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());

  // Encrypted-but-unauthenticated is better than clear-but-authenticated.
  service_->SetSecurity(Service::kCryptoNone, false, true);
  service2_->SetSecurity(Service::kCryptoAes, false, false);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());

  // For same encryption, prefer key rotation.
  service_->SetSecurity(Service::kCryptoRc4, false, false);
  service2_->SetSecurity(Service::kCryptoRc4, true, false);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());

  // For same encryption, prefer authenticated AP.
  service_->SetSecurity(Service::kCryptoRc4, false, false);
  service2_->SetSecurity(Service::kCryptoRc4, false, true);
  EXPECT_GT(service2_->SecurityLevel(), service_->SecurityLevel());
}

TEST_F(ServiceTest, SetErrorDetails) {
  EXPECT_EQ(Service::kErrorDetailsNone, service_->error_details());
  static const char kDetails[] = "Certificate revoked.";
  ServiceMockAdaptor* adaptor = GetAdaptor();
  EXPECT_CALL(*adaptor, EmitStringChanged(kErrorDetailsProperty, kDetails));
  service_->SetErrorDetails(Service::kErrorDetailsNone);
  EXPECT_EQ(Service::kErrorDetailsNone, service_->error_details());
  service_->SetErrorDetails(kDetails);
  EXPECT_EQ(kDetails, service_->error_details());
  service_->SetErrorDetails(kDetails);
}

TEST_F(ServiceTest, SetAutoConnectFull) {
  EXPECT_FALSE(service_->auto_connect());
  Error error;
  EXPECT_FALSE(GetAutoConnect(&error));
  EXPECT_TRUE(error.IsSuccess());

  // false -> false
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(0);
  SetAutoConnectFull(false, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_FALSE(GetAutoConnect(nullptr));
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // Clear the |retain_auto_connect_| flag for the next test.
  service_->Unload();
  ASSERT_FALSE(service_->retain_auto_connect());

  // false -> true
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(1);
  SetAutoConnectFull(true, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(service_->auto_connect());
  EXPECT_TRUE(GetAutoConnect(nullptr));
  EXPECT_TRUE(service_->retain_auto_connect());
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // Clear the |retain_auto_connect_| flag for the next test.
  service_->Unload();
  ASSERT_FALSE(service_->retain_auto_connect());

  // true -> true
  service_->SetAutoConnect(true);
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(0);
  SetAutoConnectFull(true, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(service_->auto_connect());
  EXPECT_TRUE(GetAutoConnect(nullptr));
  EXPECT_TRUE(service_->retain_auto_connect());
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // Clear the |retain_auto_connect_| flag for the next test.
  service_->Unload();
  ASSERT_FALSE(service_->retain_auto_connect());

  // true -> false
  service_->SetAutoConnect(true);
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(1);
  SetAutoConnectFull(false, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(GetAutoConnect(nullptr));
  EXPECT_TRUE(service_->retain_auto_connect());
  Mock::VerifyAndClearExpectations(&mock_manager_);
}

TEST_F(ServiceTest, SetAutoConnectFullUserUpdatePersists) {
  // If the user sets the kAutoConnectProperty explicitly, the preference must
  // be persisted, even if the property was not changed.
  Error error;
  MockProfileRefPtr mock_profile(new MockProfile(&mock_manager_));
  FakeStore storage;
  service_->set_profile(mock_profile);
  service_->SetAutoConnect(true);

  EXPECT_CALL(*mock_profile, UpdateService(_));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(&storage));
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(Pointer(service_)))
      .WillOnce(Return(false));
  EXPECT_FALSE(service_->retain_auto_connect());
  SetAutoConnectFull(true, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());
}

TEST_F(ServiceTest, ClearAutoConnect) {
  EXPECT_FALSE(service_->auto_connect());
  Error error;
  EXPECT_FALSE(GetAutoConnect(&error));
  EXPECT_TRUE(error.IsSuccess());

  // unset -> false
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(0);
  ClearAutoConnect(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(GetAutoConnect(nullptr));
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // false -> false
  SetAutoConnectFull(false, &error);
  EXPECT_FALSE(GetAutoConnect(nullptr));
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(0);
  ClearAutoConnect(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(GetAutoConnect(nullptr));
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // true -> false
  SetAutoConnectFull(true, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(GetAutoConnect(nullptr));
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(1);
  ClearAutoConnect(&error);
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(GetAutoConnect(nullptr));
  Mock::VerifyAndClearExpectations(&mock_manager_);
}

TEST_F(ServiceTest, UniqueAttributes) {
  EXPECT_NE(service_->serial_number_, service2_->serial_number_);
}

TEST_F(ServiceTest, PropertyChanges) {
  TestCommonPropertyChanges(service_, GetAdaptor());
  TestAutoConnectPropertyChange(service_, GetAdaptor());
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(ServiceTest, CustomSetterNoopChange) {
  TestCustomSetterNoopChange(service_, &mock_manager_);
}

TEST_F(ServiceTest, GetTethering) {
  EXPECT_EQ(Service::TetheringState::kUnknown, service_->GetTethering());
}

TEST_F(ServiceTest, MeteredOverride) {
  Error error;
  service_->SetMeteredProperty(true, &error);
  EXPECT_TRUE(service_->IsMetered());

  service_->SetMeteredProperty(false, &error);
  EXPECT_FALSE(service_->IsMetered());
}

TEST_F(ServiceTest, SaveMeteredOverride) {
  FakeStore storage;
  EXPECT_TRUE(service_->Save(&storage));

  // Newly created services should not have a metered override value
  // since that is set by the user, and should thus have no value
  // to save to the storage.
  bool metered_override = false;
  EXPECT_FALSE(storage.GetBool(storage_id_, Service::kStorageMeteredOverride,
                               &metered_override));

  Error error;
  service_->SetMeteredProperty(true, &error);
  EXPECT_TRUE(service_->Save(&storage));
  EXPECT_TRUE(storage.GetBool(storage_id_, Service::kStorageMeteredOverride,
                              &metered_override));
  EXPECT_TRUE(metered_override);
}

TEST_F(ServiceTest, IsNotMeteredByDefault) {
  EXPECT_FALSE(service_->IsMetered());
}

class ServiceWithMockOnPropertyChanged : public ServiceUnderTest {
 public:
  explicit ServiceWithMockOnPropertyChanged(Manager* manager)
      : ServiceUnderTest(manager) {}
  MOCK_METHOD(void, OnPropertyChanged, (std::string_view), (override));
};

TEST_F(ServiceTest, ConfigureServiceTriggersOnPropertyChanged) {
  auto service(
      base::MakeRefCounted<ServiceWithMockOnPropertyChanged>(&mock_manager_));
  KeyValueStore args;
  args.Set<std::string>(kUIDataProperty, "terpsichorean ejectamenta");
  args.Set<bool>(kSaveCredentialsProperty, false);

  // Calling Configure with different values from before triggers a single
  // OnPropertyChanged call per property.
  EXPECT_CALL(*service, OnPropertyChanged(Eq(kUIDataProperty))).Times(1);
  EXPECT_CALL(*service, OnPropertyChanged(Eq(kSaveCredentialsProperty)))
      .Times(1);
  {
    Error error;
    service->Configure(args, &error);
    EXPECT_TRUE(error.IsSuccess());
  }
  Mock::VerifyAndClearExpectations(service.get());

  // Calling Configure with the same values as before should not trigger
  // OnPropertyChanged().
  EXPECT_CALL(*service, OnPropertyChanged(_)).Times(0);
  {
    Error error;
    service->Configure(args, &error);
    EXPECT_TRUE(error.IsSuccess());
  }
}

TEST_F(ServiceTest, ClearExplicitlyDisconnected) {
  EXPECT_FALSE(GetExplicitlyDisconnected());
  EXPECT_CALL(mock_manager_, UpdateService(_)).Times(0);
  service_->ClearExplicitlyDisconnected();
  Mock::VerifyAndClearExpectations(&mock_manager_);

  SetExplicitlyDisconnected(true);
  EXPECT_CALL(mock_manager_, UpdateService(Pointer(service_)));
  service_->ClearExplicitlyDisconnected();
  Mock::VerifyAndClearExpectations(&mock_manager_);
  EXPECT_FALSE(GetExplicitlyDisconnected());
}

TEST_F(ServiceTest, Compare) {
  std::vector<scoped_refptr<MockService>> mock_services;
  for (size_t i = 0; i < 11; ++i) {
    mock_services.push_back(new NiceMock<MockService>(&mock_manager_));
  }
  scoped_refptr<MockService> service2 = mock_services[2];
  scoped_refptr<MockService> service10 = mock_services[10];
  mock_services.clear();

  // Services should already be sorted by |serial_number_|.
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Two otherwise equal services should be reordered by strength
  service10->SetStrength(1);
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // A service that has been connected before should be considered
  // above a service that has never been connected to before.
  service2->has_ever_connected_ = true;
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  scoped_refptr<MockProfile> profile2(new MockProfile(manager(), ""));
  scoped_refptr<MockProfile> profile10(new MockProfile(manager(), ""));

  service2->set_profile(profile2);
  service10->set_profile(profile10);

  // When comparing two services with different profiles, prefer the one
  // that is not ephemeral.
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(Pointer(service2)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(Pointer(service10)))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // Prefer the service with the more recently applied profile if neither
  // service is ephemeral.
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_manager_,
              IsProfileBefore(Pointer(profile2), Pointer(profile10)))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_manager_,
              IsProfileBefore(Pointer(profile10), Pointer(profile2)))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Security.
  service10->SetSecurity(Service::kCryptoAes, true, true);
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Auto-connect.
  service2->SetAutoConnect(true);
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Managed credentials
  service10->managed_credentials_ = true;
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Priority.
  service2->SetPriority(1, nullptr);
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));
  service10->SetPriority(2, nullptr);
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Technology.
  EXPECT_CALL(*service2, technology())
      .WillRepeatedly(Return(Technology::kEthernet));
  EXPECT_CALL(*service10, technology())
      .WillRepeatedly(Return((Technology::kWiFi)));

  technology_order_for_sorting_ = {Technology::kEthernet, Technology::kWiFi};
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Connectable.
  service10->SetConnectable(true);
  service2->SetConnectable(false);
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // IsFailed.
  EXPECT_CALL(*service2, state()).WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*service2, IsFailed()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service10, state())
      .WillRepeatedly(Return(Service::kStateFailure));
  EXPECT_CALL(*service10, IsFailed()).WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Connecting.
  EXPECT_CALL(*service10, state())
      .WillRepeatedly(Return(Service::kStateAssociating));
  EXPECT_CALL(*service10, IsConnecting()).WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Connected-but-portalled preferred over unconnected.
  EXPECT_CALL(*service2, state())
      .WillRepeatedly(Return(Service::kStateNoConnectivity));
  EXPECT_CALL(*service2, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Connected preferred over connected-but-portalled.
  service10->SetConnectable(false);
  service2->SetConnectable(true);
  EXPECT_CALL(*service10, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*service10, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Online preferred over just connected.
  EXPECT_CALL(*service2, state()).WillRepeatedly(Return(Service::kStateOnline));
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));

  // Connectivity state ignored if this is specified.
  const bool kDoNotCompareConnectivityState = false;
  EXPECT_TRUE(
      SortingOrderIs(service2, service10, kDoNotCompareConnectivityState));
}

TEST_F(ServiceTest, ComparePreferEthernetOverWifi) {
  // Create mock ethernet service.
  scoped_refptr<MockService> ethernet_service(
      new NiceMock<MockService>(manager()));
  EXPECT_CALL(*ethernet_service.get(), technology())
      .WillRepeatedly(Return(Technology::kEthernet));

  // Create mock wifi service.
  scoped_refptr<MockService> wifi_service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*wifi_service.get(), technology())
      .WillRepeatedly(Return((Technology::kWiFi)));

  // Confirm that ethernet service is sorted above wifi service.
  technology_order_for_sorting_ = {Technology::kEthernet, Technology::kWiFi};
  EXPECT_TRUE(DefaultSortingOrderIs(ethernet_service, wifi_service));

  // Even making the wifi service managed doesn't change the network sorting
  // order.
  wifi_service->managed_credentials_ = true;
  EXPECT_TRUE(DefaultSortingOrderIs(ethernet_service, wifi_service));
}

TEST_F(ServiceTest, CompareSources) {
  // Check that given the exactly same other parameters,
  // services with different sources are sorted properly.
  scoped_refptr<MockService> service1(new NiceMock<MockService>(manager()));
  scoped_refptr<MockService> service2(new NiceMock<MockService>(manager()));
  service1->source_ = Service::ONCSource::kONCSourceUnknown;
  service2->source_ = Service::ONCSource::kONCSourceNone;
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service1));

  service1->source_ = Service::ONCSource::kONCSourceUserImport;
  EXPECT_TRUE(DefaultSortingOrderIs(service1, service2));

  service2->source_ = Service::ONCSource::kONCSourceDevicePolicy;
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service1));

  service1->source_ = Service::ONCSource::kONCSourceUserPolicy;
  EXPECT_TRUE(DefaultSortingOrderIs(service1, service2));
}

TEST_F(ServiceTest, SanitizeStorageIdentifier) {
  EXPECT_EQ("", Service::SanitizeStorageIdentifier(""));

  for (int c = 0; c < 256; ++c) {
    std::string identifier(1, c);
    std::string sanitized_identifier = std::isalnum(c) ? identifier : "_";
    EXPECT_EQ(sanitized_identifier,
              Service::SanitizeStorageIdentifier(identifier));
  }

  EXPECT_EQ("service_1_2_3_2_Fake_Net_",
            Service::SanitizeStorageIdentifier("service_1-2:3.2_Fake^Net!"));
}

TEST_F(ServiceTest, DisconnectSetsDisconnectState) {
  EXPECT_EQ(service_->state(), Service::kStateIdle);

  // Inactive Service will immediately fail a Disconnect call.
  Error error;
  service_->Disconnect(&error, __func__);
  EXPECT_EQ(error.type(), Error::kNotConnected);
  EXPECT_EQ(service_->state(), Service::kStateIdle);

  // Non-disconnectable Service will also immediately fail a Disconnect call.
  service_->SetDisconnectable(false);
  error.Reset();
  service_->Disconnect(&error, __func__);
  EXPECT_EQ(error.type(), Error::kNotConnected);
  EXPECT_EQ(service_->state(), Service::kStateIdle);

  service_->SetDisconnectable(true);

  // Otherwise the state will be driven to kStateDisconnecting.
  service_->SetState(Service::kStateAssociating);
  error.Reset();
  service_->Disconnect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(service_->state(), Service::kStateDisconnecting);
}

TEST_F(ServiceTest, DelayedDisconnect) {
  // Any state that causes IsActive to return true will do.
  service_->SetState(Service::kStateAssociating);

  // Begin disconnect but do not finish.
  Error error;
  service_->Disconnect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(service_->state(), Service::kStateDisconnecting);

  // Trigger connection.
  ASSERT_TRUE(service_->connectable());
  service_->Connect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(service_->state(), Service::kStateDisconnecting);
  EXPECT_TRUE(HasPendingConnect());

  // Finish the disconnection by driving state to kStateIdle.
  service_->SetState(Service::kStateIdle);
  EXPECT_TRUE(HasPendingConnect());

  // Invoke the pending disconnect task.
  GetPendingConnectTask().callback().Run();
  EXPECT_FALSE(HasPendingConnect());
}

TEST_F(ServiceTest, DelayedDisconnectWithAdditionalConnect) {
  // Any state that causes IsActive to return true will do.
  service_->SetState(Service::kStateAssociating);

  // Begin disconnect but do not finish.
  Error error;
  service_->Disconnect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(service_->state(), Service::kStateDisconnecting);

  // Trigger connection.
  ASSERT_TRUE(service_->connectable());
  service_->Connect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(service_->state(), Service::kStateDisconnecting);
  EXPECT_TRUE(HasPendingConnect());

  // Finish the disconnection by driving state to kStateIdle.
  service_->SetState(Service::kStateIdle);
  EXPECT_TRUE(HasPendingConnect());

  // Trigger connection prior to the pending connect task being run;
  // ensure that the pending connect task will be cancelled.
  ASSERT_TRUE(service_->connectable());
  service_->Connect(&error, __func__);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(HasPendingConnect());
}

TEST_F(ServiceTest, RequestPortalDetection) {
  SetStateField(Service::kStateConnected);
  auto network =
      std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled).Times(0);
  EXPECT_CALL(*network, RequestNetworkValidation(
                            NetworkMonitor::ValidationReason::kDBusRequest));

  Error error;
  service_->RequestPortalDetection(&error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, TrafficCountersRefreshWithMultipleSources) {
  Network::TrafficCounterMap initial_snapshot;
  initial_snapshot[patchpanel::Client::TrafficSource::kChrome] = {
      .rx_bytes = 12, .tx_bytes = 34, .rx_packets = 56, .tx_packets = 78};
  initial_snapshot[patchpanel::Client::TrafficSource::kUser] = {
      .rx_bytes = 90, .tx_bytes = 87, .rx_packets = 65, .tx_packets = 43};
  Network::TrafficCounterMap initial_extra_snapshot;
  service_->InitializeTrafficCounterSnapshots(initial_snapshot,
                                              initial_extra_snapshot);

  EXPECT_EQ(service_->current_total_traffic_counters().size(), 0);

  Network::TrafficCounterMap refresh_snapshot;
  refresh_snapshot[patchpanel::Client::TrafficSource::kChrome] = {
      .rx_bytes = 20, .tx_bytes = 40, .rx_packets = 60, .tx_packets = 80};
  refresh_snapshot[patchpanel::Client::TrafficSource::kUser] = {
      .rx_bytes = 100, .tx_bytes = 90, .rx_packets = 80, .tx_packets = 70};
  Network::TrafficCounterMap refresh_extra_snapshot;
  service_->RefreshTrafficCounters(refresh_snapshot, refresh_extra_snapshot);

  EXPECT_EQ(service_->current_total_traffic_counters().size(), 2);
  patchpanel::Client::TrafficVector chrome_counters_diff = {
      .rx_bytes = 8, .tx_bytes = 6, .rx_packets = 4, .tx_packets = 2};
  patchpanel::Client::TrafficVector user_counters_diff = {
      .rx_bytes = 10, .tx_bytes = 3, .rx_packets = 15, .tx_packets = 27};
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kChrome],
            chrome_counters_diff);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kUser],
            user_counters_diff);
}

TEST_F(ServiceTest, RequestTrafficCountersWithAttachedNetwork) {
  auto source0 = patchpanel::Client::TrafficSource::kChrome;
  auto source1 = patchpanel::Client::TrafficSource::kUser;
  auto source2 = patchpanel::Client::TrafficSource::kArc;

  // Initial counter snapshot
  patchpanel::Client::TrafficVector init_counter_arr0 = {200, 410, 5, 8};
  patchpanel::Client::TrafficVector init_counter_arr1 = {1432, 3451, 7, 10};
  Network::TrafficCounterMap init_counters;
  init_counters[source0] = init_counter_arr0;
  init_counters[source1] = init_counter_arr1;

  auto network = std::make_unique<MockNetwork>(1, kIfName, Technology::kWiFi);
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(init_counters);
      }));

  service_->AttachNetwork(network->AsWeakPtr());
  Mock::VerifyAndClearExpectations(network.get());

  // Traffic counter snapshot at the time of the next request.
  patchpanel::Client::TrafficVector counter_arr0 = {
      .rx_bytes = 212, .tx_bytes = 434, .rx_packets = 6, .tx_packets = 10};
  patchpanel::Client::TrafficVector counter_arr1 = {
      .rx_bytes = 1856, .tx_bytes = 5612, .rx_packets = 15, .tx_packets = 12};
  patchpanel::Client::TrafficVector counter_arr2 = {
      .rx_bytes = 1123, .tx_bytes = 2390, .rx_packets = 10, .tx_packets = 8};
  Network::TrafficCounterMap counters;
  counters[source0] = counter_arr0;
  counters[source1] = counter_arr1;
  counters[source2] = counter_arr2;
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(counters);
      }));

  bool successfully_requested_traffic_counters = false;
  std::vector<brillo::VariantDictionary> actual_traffic_counters;
  service_->RequestTrafficCounters(base::BindOnce(
      [](bool* success, std::vector<brillo::VariantDictionary>* output,
         const Error& error,
         const std::vector<brillo::VariantDictionary>& input) {
        *success = error.IsSuccess();
        output->assign(input.begin(), input.end());
      },
      &successfully_requested_traffic_counters, &actual_traffic_counters));

  // Expect to see the delta of the later snapshot minus the initial snapshot
  EXPECT_TRUE(successfully_requested_traffic_counters);
  for (const auto& dict : actual_traffic_counters) {
    EXPECT_EQ(3, dict.size());
    EXPECT_TRUE(base::Contains(dict, "source"));
    EXPECT_TRUE(base::Contains(dict, "rx_bytes"));
    EXPECT_TRUE(base::Contains(dict, "tx_bytes"));

    if (dict.at("source").TryGet<std::string>() == "CHROME") {
      EXPECT_EQ(12, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(24, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "USER") {
      EXPECT_EQ(424, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(2161, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "ARC") {
      EXPECT_EQ(1123, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(2390, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    FAIL() << "Unexpected source " << dict.at("source").TryGet<std::string>();
  }
}

TEST_F(ServiceTest, RequestTrafficCountersWithExtraSource) {
  auto network_source = patchpanel::Client::TrafficSource::kChrome;
  auto extra_source = patchpanel::Client::TrafficSource::kTethering;

  // Initial network traffic counters snapshot
  Network::TrafficCounterMap network_counters;
  network_counters[network_source] = {100, 200, 10, 20};
  auto network = std::make_unique<MockNetwork>(1, kIfName, Technology::kWiFi);
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));

  // Initial extra traffic counters snapshot
  Network::TrafficCounterMap extra_counters;
  extra_counters[extra_source] = {40, 50, 4, 5};
  service_->SetExtraTrafficCounters(extra_counters);

  // Initial Network attach
  service_->AttachNetwork(network->AsWeakPtr());
  Mock::VerifyAndClearExpectations(network.get());
  EXPECT_EQ(service_->current_total_traffic_counters().size(), 0);

  // Network traffic counter snapshot at the time of the next request.
  network_counters.clear();
  network_counters[network_source] = {140, 250, 14, 25};
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));

  // Extra traffic counter snapshot at the time of the next request.
  extra_counters.clear();
  extra_counters[extra_source] = {80, 95, 8, 9};
  service_->SetExtraTrafficCounters(extra_counters);

  // DBus request to refresh and get counters
  bool successfully_requested_traffic_counters = false;
  std::vector<brillo::VariantDictionary> actual_traffic_counters;
  service_->RequestTrafficCounters(base::BindOnce(
      [](bool* success, std::vector<brillo::VariantDictionary>* output,
         const Error& error,
         const std::vector<brillo::VariantDictionary>& input) {
        *success = error.IsSuccess();
        output->assign(input.begin(), input.end());
      },
      &successfully_requested_traffic_counters, &actual_traffic_counters));
  Mock::VerifyAndClearExpectations(network.get());

  // Expect to see the delta of the later snapshot minus the initial snapshot
  EXPECT_TRUE(successfully_requested_traffic_counters);
  for (const auto& dict : actual_traffic_counters) {
    EXPECT_EQ(3, dict.size());
    EXPECT_TRUE(base::Contains(dict, "source"));
    EXPECT_TRUE(base::Contains(dict, "rx_bytes"));
    EXPECT_TRUE(base::Contains(dict, "tx_bytes"));

    if (dict.at("source").TryGet<std::string>() == "CHROME") {
      EXPECT_EQ(40, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(50, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "TETHERING") {
      EXPECT_EQ(40, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(45, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    FAIL() << "Unexpected source " << dict.at("source").TryGet<std::string>();
  }
}

TEST_F(ServiceTest, RequestTrafficCountersAfterDetachingNetwork) {
  auto source0 = patchpanel::Client::TrafficSource::kChrome;
  auto source1 = patchpanel::Client::TrafficSource::kUser;
  auto source2 = patchpanel::Client::TrafficSource::kArc;

  // Initial counter snapshot
  patchpanel::Client::TrafficVector init_counter_arr0 = {200, 410, 5, 8};
  patchpanel::Client::TrafficVector init_counter_arr1 = {1432, 3451, 7, 10};
  Network::TrafficCounterMap init_counters;
  init_counters[source0] = init_counter_arr0;
  init_counters[source1] = init_counter_arr1;
  auto network = std::make_unique<MockNetwork>(1, kIfName, Technology::kWiFi);
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(init_counters);
      }));

  service_->AttachNetwork(network->AsWeakPtr());
  Mock::VerifyAndClearExpectations(network.get());

  // Final traffic counter snapshot.
  patchpanel::Client::TrafficVector final_counter_arr0 = {
      .rx_bytes = 212, .tx_bytes = 434, .rx_packets = 6, .tx_packets = 10};
  patchpanel::Client::TrafficVector final_counter_arr1 = {
      .rx_bytes = 1856, .tx_bytes = 5612, .rx_packets = 15, .tx_packets = 12};
  patchpanel::Client::TrafficVector final_counter_arr2 = {
      .rx_bytes = 1123, .tx_bytes = 2390, .rx_packets = 10, .tx_packets = 8};
  Network::TrafficCounterMap counters;
  counters[source0] = final_counter_arr0;
  counters[source1] = final_counter_arr1;
  counters[source2] = final_counter_arr2;
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(counters);
      }));

  service_->DetachNetwork();
  Mock::VerifyAndClearExpectations(network.get());

  EXPECT_CALL(*network, RequestTrafficCounters).Times(0);
  bool successfully_requested_traffic_counters = false;
  std::vector<brillo::VariantDictionary> actual_traffic_counters;
  service_->RequestTrafficCounters(base::BindOnce(
      [](bool* success, std::vector<brillo::VariantDictionary>* output,
         const Error& error,
         const std::vector<brillo::VariantDictionary>& input) {
        *success = error.IsSuccess();
        output->assign(input.begin(), input.end());
      },
      &successfully_requested_traffic_counters, &actual_traffic_counters));

  // Expect to see the delta of the final snapshot minus the initial snapshot
  EXPECT_TRUE(successfully_requested_traffic_counters);
  for (const auto& dict : actual_traffic_counters) {
    EXPECT_EQ(3, dict.size());
    EXPECT_TRUE(base::Contains(dict, "source"));
    EXPECT_TRUE(base::Contains(dict, "rx_bytes"));
    EXPECT_TRUE(base::Contains(dict, "tx_bytes"));

    if (dict.at("source").TryGet<std::string>() == "CHROME") {
      EXPECT_EQ(12, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(24, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "USER") {
      EXPECT_EQ(424, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(2161, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "ARC") {
      EXPECT_EQ(1123, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(2390, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    FAIL() << "Unexpected source " << dict.at("source").TryGet<std::string>();
  }
}

TEST_F(ServiceTest, RequestTrafficCountersWithoutAttachedNetwork) {
  auto chromeRxBytesStorageKey = Service::GetCurrentTrafficCounterKey(
      patchpanel::Client::TrafficSource::kChrome,
      Service::kStorageTrafficCounterRxBytesSuffix);
  auto chromeTxBytesStorageKey = Service::GetCurrentTrafficCounterKey(
      patchpanel::Client::TrafficSource::kChrome,
      Service::kStorageTrafficCounterTxBytesSuffix);
  auto userRxBytesStorageKey = Service::GetCurrentTrafficCounterKey(
      patchpanel::Client::TrafficSource::kUser,
      Service::kStorageTrafficCounterRxBytesSuffix);
  auto userTxBytesStorageKey = Service::GetCurrentTrafficCounterKey(
      patchpanel::Client::TrafficSource::kUser,
      Service::kStorageTrafficCounterTxBytesSuffix);

  FakeStore storage;
  storage.SetUint64(storage_id_, chromeRxBytesStorageKey, 48926);
  storage.SetUint64(storage_id_, chromeTxBytesStorageKey, 9324);
  storage.SetUint64(storage_id_, userRxBytesStorageKey, 4532);
  storage.SetUint64(storage_id_, userTxBytesStorageKey, 1234);
  EXPECT_TRUE(service_->Load(&storage));

  bool successfully_requested_traffic_counters = false;
  std::vector<brillo::VariantDictionary> actual_traffic_counters;
  service_->RequestTrafficCounters(base::BindOnce(
      [](bool* success, std::vector<brillo::VariantDictionary>* output,
         const Error& error,
         const std::vector<brillo::VariantDictionary>& input) {
        *success = error.IsSuccess();
        output->assign(input.begin(), input.end());
      },
      &successfully_requested_traffic_counters, &actual_traffic_counters));

  EXPECT_TRUE(successfully_requested_traffic_counters);
  EXPECT_EQ(2, actual_traffic_counters.size());
  for (const auto& dict : actual_traffic_counters) {
    EXPECT_EQ(3, dict.size());
    EXPECT_TRUE(base::Contains(dict, "source"));
    EXPECT_TRUE(base::Contains(dict, "rx_bytes"));
    EXPECT_TRUE(base::Contains(dict, "tx_bytes"));

    if (dict.at("source").TryGet<std::string>() == "CHROME") {
      EXPECT_EQ(48926, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(9324, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    if (dict.at("source").TryGet<std::string>() == "USER") {
      EXPECT_EQ(4532, dict.at("rx_bytes").TryGet<uint64_t>());
      EXPECT_EQ(1234, dict.at("tx_bytes").TryGet<uint64_t>());
      continue;
    }

    FAIL() << "Unexpected source " << dict.at("source").TryGet<std::string>();
  }
}

TEST_F(ServiceTest, RequestTrafficCountersBackgroundRefresh) {
  auto source0 = patchpanel::Client::TrafficSource::kChrome;
  auto source1 = patchpanel::Client::TrafficSource::kUser;
  auto source2 = patchpanel::Client::TrafficSource::kArc;

  // Initial counter snapshot
  patchpanel::Client::TrafficVector init_counter_arr0 = {200, 410, 5, 8};
  patchpanel::Client::TrafficVector init_counter_arr1 = {1432, 3451, 7, 10};
  Network::TrafficCounterMap init_counters;
  init_counters[source0] = init_counter_arr0;
  init_counters[source1] = init_counter_arr1;
  auto network = std::make_unique<MockNetwork>(1, kIfName, Technology::kWiFi);
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(init_counters);
      }));

  service_->AttachNetwork(network->AsWeakPtr());
  Mock::VerifyAndClearExpectations(network.get());

  // Traffic counter snapshot at the time of the next background refresh.
  patchpanel::Client::TrafficVector counter_arr0 = {
      .rx_bytes = 212, .tx_bytes = 434, .rx_packets = 6, .tx_packets = 10};
  patchpanel::Client::TrafficVector counter_arr1 = {
      .rx_bytes = 1856, .tx_bytes = 5612, .rx_packets = 15, .tx_packets = 12};
  patchpanel::Client::TrafficVector counter_arr2 = {
      .rx_bytes = 1123, .tx_bytes = 2390, .rx_packets = 10, .tx_packets = 8};
  Network::TrafficCounterMap counters;
  counters[source0] = counter_arr0;
  counters[source1] = counter_arr1;
  counters[source2] = counter_arr2;
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(counters);
      }));
  dispatcher()->task_environment().FastForwardBy(
      Service::kTrafficCountersRefreshInterval);
  dispatcher()->task_environment().FastForwardBy(base::Seconds(1));
  Mock::VerifyAndClearExpectations(network.get());

  // Expect to see the delta of the later snapshot minus the initial snapshot
  for (const auto& [source, counters] :
       service_->current_total_traffic_counters()) {
    if (source == source0) {
      EXPECT_EQ(12, counters.rx_bytes);
      EXPECT_EQ(24, counters.tx_bytes);
      continue;
    }

    if (source == source1) {
      EXPECT_EQ(424, counters.rx_bytes);
      EXPECT_EQ(2161, counters.tx_bytes);
      continue;
    }

    if (source == source2) {
      EXPECT_EQ(1123, counters.rx_bytes);
      EXPECT_EQ(2390, counters.tx_bytes);
      continue;
    }

    FAIL() << "Unexpected source " << source << " for counters " << counters;
  }

  // The Network is detached and the Service disconnects, there is one final
  // traffic counter refresh expected
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(counters);
      }));
  service_->DetachNetwork();
  Mock::VerifyAndClearExpectations(network.get());

  // There is no background refresh when the Service has no attached Network.
  EXPECT_CALL(*network, RequestTrafficCounters).Times(0);
  dispatcher()->task_environment().FastForwardBy(
      Service::kTrafficCountersRefreshInterval);
  dispatcher()->task_environment().FastForwardBy(base::Seconds(1));
  Mock::VerifyAndClearExpectations(network.get());
}

TEST_F(ServiceTest, ResetTrafficCounters) {
  auto source0 = patchpanel::Client::TrafficSource::kChrome;
  auto source1 = patchpanel::Client::TrafficSource::kUser;
  auto extra_source = patchpanel::Client::TrafficSource::kTethering;

  auto network = std::make_unique<MockNetwork>(1, kIfName, Technology::kWiFi);

  // Initialize the Service's traffic counter snapshot.
  Network::TrafficCounterMap network_counters;
  network_counters[source0] = {10, 20, 30, 40};
  network_counters[source1] = {50, 60, 70, 80};
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));

  // Initial extra traffic counters snapshot
  Network::TrafficCounterMap extra_counters;
  extra_counters[extra_source] = {100, 200, 10, 20};
  service_->SetExtraTrafficCounters(extra_counters);

  // Initial Network attach
  service_->AttachNetwork(network->AsWeakPtr());
  Mock::VerifyAndClearExpectations(network.get());
  EXPECT_EQ(service_->current_total_traffic_counters().size(), 0);

  // Refresh traffic counters with a DBus request, updating the traffic counter
  // snapshot and current traffic counters.
  network_counters.clear();
  network_counters[source0] = {100, 200, 300, 400};
  network_counters[source1] = {500, 600, 700, 800};
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));

  extra_counters.clear();
  extra_counters[extra_source] = {110, 220, 11, 22};
  service_->SetExtraTrafficCounters(extra_counters);

  service_->RequestTrafficCounters(base::DoNothing());
  Mock::VerifyAndClearExpectations(network.get());

  EXPECT_EQ(service_->current_total_traffic_counters().size(), 3);
  patchpanel::Client::TrafficVector chrome_counters_diff = {90, 180, 270, 360};
  patchpanel::Client::TrafficVector user_counters_diff = {450, 540, 630, 720};
  patchpanel::Client::TrafficVector tethering_counters_diff = {10, 20, 1, 2};
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kChrome],
            chrome_counters_diff);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kUser],
            user_counters_diff);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kTethering],
            tethering_counters_diff);

  // Reset the traffic counters. New snapshots are set.
  network_counters.clear();
  network_counters[source0] = {110, 220, 330, 440};
  network_counters[source1] = {550, 660, 770, 880};
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));
  extra_counters.clear();
  extra_counters[extra_source] = {120, 230, 12, 23};
  service_->SetExtraTrafficCounters(extra_counters);

  service_->ResetTrafficCounters(/*error=*/nullptr);
  Mock::VerifyAndClearExpectations(network.get());
  EXPECT_EQ(service_->current_total_traffic_counters().size(), 0);

  // Refresh traffic counters a second time, updating the traffic counter
  // snapshot and current traffic counters.
  network_counters.clear();
  network_counters[source0] = {1000, 2000, 3000, 4000};
  network_counters[source1] = {5000, 6000, 7000, 8000};
  EXPECT_CALL(*network, RequestTrafficCounters)
      .WillOnce(WithArgs<0>([&](Network::GetTrafficCountersCallback callback) {
        std::move(callback).Run(network_counters);
      }));
  extra_counters.clear();
  extra_counters[extra_source] = {200, 300, 20, 30};
  service_->SetExtraTrafficCounters(extra_counters);

  service_->RequestTrafficCounters(base::DoNothing());
  Mock::VerifyAndClearExpectations(network.get());

  chrome_counters_diff = {890, 1780, 2670, 3560};
  user_counters_diff = {4450, 5340, 6230, 7120};
  tethering_counters_diff = {80, 70, 8, 7};
  EXPECT_EQ(service_->current_total_traffic_counters().size(), 3);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kChrome],
            chrome_counters_diff);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kUser],
            user_counters_diff);
  EXPECT_EQ(service_->current_total_traffic_counters()
                [patchpanel::Client::TrafficSource::kTethering],
            tethering_counters_diff);
}

TEST_F(ServiceTest, UpdateLinkSpeed) {
  EXPECT_CALL(*GetAdaptor(), EmitIntChanged(kUplinkSpeedPropertyKbps, 10))
      .Times(1);
  EXPECT_CALL(*GetAdaptor(), EmitIntChanged(kDownlinkSpeedPropertyKbps, 20))
      .Times(1);

  SetUplinkSpeedKbps(10);
  SetDownlinkSpeedKbps(20);

  EXPECT_EQ(uplink_speed_kbps(), 10);
  EXPECT_EQ(downlink_speed_kbps(), 20);
}

TEST_F(ServiceTest, UpdateLinkSpeedTwice) {
  // Check link speed is only set once when the setter is called twice on
  // identical value.
  EXPECT_CALL(*GetAdaptor(), EmitIntChanged(kUplinkSpeedPropertyKbps, _))
      .Times(1);

  SetUplinkSpeedKbps(30);
  SetUplinkSpeedKbps(30);

  EXPECT_EQ(uplink_speed_kbps(), 30);
}

TEST_F(ServiceTest, ServiceMetricsTimeToConfig) {
  EXPECT_CALL(*metrics(), SendToUMA("Network.Shill.Wifi.TimeToConfig", Ge(0),
                                    Metrics::kTimerHistogramMillisecondsMin,
                                    Metrics::kTimerHistogramMillisecondsMax,
                                    Metrics::kTimerHistogramNumBuckets));
  service_->UpdateStateTransitionMetrics(Service::kStateConfiguring);
  service_->UpdateStateTransitionMetrics(Service::kStateConnected);
}

TEST_F(ServiceTest, ServiceMetricsTimeToPortal) {
  EXPECT_CALL(*metrics(), SendToUMA("Network.Shill.Wifi.TimeToPortal", Ge(0),
                                    Metrics::kTimerHistogramMillisecondsMin,
                                    Metrics::kTimerHistogramMillisecondsMax,
                                    Metrics::kTimerHistogramNumBuckets));
  service_->UpdateStateTransitionMetrics(Service::kStateConnected);
  service_->UpdateStateTransitionMetrics(Service::kStateNoConnectivity);
}

TEST_F(ServiceTest, ServiceMetricsTimeToOnline) {
  EXPECT_CALL(*metrics(), SendToUMA("Network.Shill.Wifi.TimeToOnline", Ge(0),
                                    Metrics::kTimerHistogramMillisecondsMin,
                                    Metrics::kTimerHistogramMillisecondsMax,
                                    Metrics::kTimerHistogramNumBuckets));
  service_->UpdateStateTransitionMetrics(Service::kStateConnected);
  service_->UpdateStateTransitionMetrics(Service::kStateOnline);
}

TEST_F(ServiceTest, ServiceMetricsServiceFailure) {
  service_->SetFailure(Service::kFailureBadPassphrase);
  EXPECT_CALL(
      *metrics(),
      SendEnumToUMA(Metrics::kMetricNetworkServiceError, Technology::kWiFi,
                    Metrics::kNetworkServiceErrorBadPassphrase));
  service_->UpdateStateTransitionMetrics(Service::kStateFailure);
}

TEST_F(ServiceTest, PortalDetectionStops) {
  SetStateField(Service::kStateConnected);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  service_->network_event_handler()->OnNetworkValidationStop(
      1, /*is_failure=*/false);
  EXPECT_EQ(Service::kStateOnline, service_->state());
}

TEST_F(ServiceTest, PortalDetectionFailsAndStops) {
  SetStateField(Service::kStateConnected);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  service_->network_event_handler()->OnNetworkValidationStop(
      1, /*is_failure=*/true);
  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
}

TEST_F(ServiceTest, PortalDetectionStopsAfterDisconnection) {
  SetStateField(Service::kStateIdle);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  service_->network_event_handler()->OnNetworkValidationStop(
      1, /*is_failure=*/false);
  EXPECT_EQ(Service::kStateIdle, service_->state());
}

TEST_F(ServiceTest, PortalDetectionResult_AfterDisconnection) {
  SetStateField(Service::kStateIdle);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };

  ASSERT_NE(service_->network_event_handler(), nullptr);
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ("", service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_Online) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateOnline, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_OnlineSecondTry) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateOnline, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_ProbeConnectionFailure) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultConnectionFailure,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_DNSFailure) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultDNSFailure,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_DNSTimeout) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultDNSTimeout,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_Redirect) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const auto probe_url =
      *net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl);
  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kPortalRedirect,
      .probe_result_metric = Metrics::kPortalDetectorResultRedirectFound,
      .target_url = probe_url,
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateRedirectFound, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(probe_url, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_RedirectNoUrl) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const auto probe_url =
      *net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl);
  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kPortalSuspected,
      .probe_result_metric = Metrics::kPortalDetectorResultRedirectNoUrl,
      .target_url = probe_url,
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateRedirectFound, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(probe_url, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_PortalSuspected) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const auto probe_url =
      *net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl);
  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kPortalSuspected,
      .probe_result_metric = Metrics::kPortalDetectorResultHTTPSFailure,
      .target_url = probe_url,
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateRedirectFound, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(probe_url, service_->probe_url_hint());
}

TEST_F(ServiceTest, PortalDetectionResult_NoConnectivity) {
  SetStateField(Service::kStateOnline);
  auto network = std::make_unique<MockNetwork>(1, "wlan0", Technology::kWiFi);
  service_->AttachNetwork(network->AsWeakPtr());

  const NetworkMonitor::Result result{
      .origin = NetworkMonitor::ResultOrigin::kProbe,
      .num_attempts = 1,
      .validation_state = PortalDetector::ValidationState::kNoConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultUnknown,
      .target_url =
          net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpUrl),
  };
  service_->network_event_handler()->OnNetworkValidationResult(1, result);

  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, service_->probe_url_string());
  EXPECT_EQ(std::nullopt, service_->probe_url_hint());
}

TEST_F(ServiceTest, UpdateNetworkValidationMode) {
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  ON_CALL(mock_manager_, IsPortalDetectionEnabled).WillByDefault(Return(true));

  EXPECT_EQ(NetworkMonitor::ValidationMode::kFullValidation,
            service_->GetNetworkValidationMode());
  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kFullValidation));
  service_->UpdateNetworkValidationMode();
}

TEST_F(ServiceTest, UpdateNetworkValidationModeWhenDisabledByTechnology) {
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  ON_CALL(mock_manager_, IsPortalDetectionEnabled).WillByDefault(Return(false));

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kDisabled));
  EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
            service_->GetNetworkValidationMode());

  service_->UpdateNetworkValidationMode();
  EXPECT_EQ(Service::kStateOnline, service_->state());
}

TEST_F(ServiceTest, UpdateNetworkValidationModeWhenDisabledByProxy) {
  Error error;
  service_->SetProxyConfig("proxyconfiguration", &error);
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  ON_CALL(mock_manager_, IsPortalDetectionEnabled).WillByDefault(Return(true));

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kHTTPOnly));
  EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
            service_->GetNetworkValidationMode());

  service_->UpdateNetworkValidationMode();
  EXPECT_NE(Service::kStateOnline, service_->state());
}

TEST_F(ServiceTest, UpdateNetworkValidationModeWhenDisabledByCheckPortal) {
  Error error;
  service_->SetCheckPortal("false", &error);
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  ON_CALL(mock_manager_, IsPortalDetectionEnabled).WillByDefault(Return(true));

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kDisabled));
  EXPECT_EQ(NetworkMonitor::ValidationMode::kDisabled,
            service_->GetNetworkValidationMode());

  service_->UpdateNetworkValidationMode();
  EXPECT_EQ(Service::kStateOnline, service_->state());
}

TEST_F(ServiceTest, UpdateNetworkValidationModeWhenSetToHTTPOnly) {
  Error error;
  service_->SetCheckPortal("http-only", &error);
  auto network =
      std::make_unique<MockNetwork>(1, "test_ifname", Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());
  EXPECT_CALL(mock_manager_, IsPortalDetectionEnabled).Times(0);

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kHTTPOnly));
  EXPECT_EQ(NetworkMonitor::ValidationMode::kHTTPOnly,
            service_->GetNetworkValidationMode());

  service_->UpdateNetworkValidationMode();
}

TEST_F(ServiceTest, UpdateNetworkValidationSucceeds) {
  SetStateField(Service::kStateConnected);
  ON_CALL(mock_manager_, IsPortalDetectionEnabled(_))
      .WillByDefault(Return(true));
  auto network =
      std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kFullValidation));
  service_->UpdateNetworkValidationMode();
  EXPECT_EQ(Service::kStateConnected, service_->state());
}

TEST_F(ServiceTest, UpdateNetworkValidationFails) {
  SetStateField(Service::kStateConnected);
  ON_CALL(mock_manager_, IsPortalDetectionEnabled(_))
      .WillByDefault(Return(true));
  auto network =
      std::make_unique<MockNetwork>(1, kIfName, Technology::kEthernet);
  service_->AttachNetwork(network->AsWeakPtr());

  EXPECT_CALL(*network, UpdateNetworkValidationMode(
                            NetworkMonitor::ValidationMode::kFullValidation));
  service_->UpdateNetworkValidationMode();
  EXPECT_EQ(Service::kStateConnected, service_->state());

  service_->network_event_handler()->OnNetworkValidationStart(
      1, /*is_failure=*/true);
  EXPECT_EQ(Service::kStateNoConnectivity, service_->state());
}

TEST_F(ServiceTest, CheckPortalStateNames) {
  EXPECT_EQ("true", Service::CheckPortalStateToString(
                        Service::CheckPortalState::kTrue));
  EXPECT_EQ("false", Service::CheckPortalStateToString(
                         Service::CheckPortalState::kFalse));
  EXPECT_EQ("auto", Service::CheckPortalStateToString(
                        Service::CheckPortalState::kAutomatic));
}

class ServiceNetworkConfigTest : public ServiceTest {
 protected:
  static constexpr char kIPv4Addr[] = "1.2.3.4/24";
  static constexpr char kIPv4Gateway[] = "1.2.3.5";
  static constexpr char kIPv6Addr1[] = "fd00::1/64";
  static constexpr char kIPv6Addr2[] = "fd00::2/64";
  static constexpr char kIPv6Gateway[] = "fd00::3";
  static constexpr char kDNS1[] = "4.3.2.1";
  static constexpr char kDNS2[] = "4.3.2.2";
  static constexpr char kDNS3[] = "fdfd::1";
  static constexpr char kSearchDomain1[] = "domain1";
  static constexpr char kSearchDomain2[] = "domain2";
  static constexpr char kIncludedRoute1[] = "10.10.10.0/24";
  static constexpr char kIncludedRoute2[] = "fd01::/64";
  static constexpr char kExcludedRoute1[] = "fd02::/128";
  static constexpr char kExcludedRoute2[] = "10.20.30.0/24";
  static constexpr char kPref64[] = "64:ff9b::/96";
  static constexpr int kMTU = 1400;

  ServiceNetworkConfigTest()
      : network_(Network::CreateForTesting(1,
                                           "test_ifname",
                                           Technology::kEthernet,
                                           false,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr)) {}

  // Sets a fake NetworkConfig as if it was given to the network through
  // the StartOptions. The final compound NetworkConfig will be derived
  // from |config| according to the normal CompoundNetworkConfig::Recalculate
  // logic.
  void SetLinkProtocolNetworkConfigOnNetwork(const NetworkConfig& config) {
    network_->set_link_protocol_network_config_for_testing(
        std::make_unique<NetworkConfig>(config));
  }

  // Sets a fake NetworkConfig as if it was received through SLAAC. The final
  // compound NetworkConfig will be derived from |config| according to the
  // normal CompoundNetworkConfig::Recalculate logic.
  void SetSLAACNetworkConfigOnNetwork(const NetworkConfig& config) {
    network_->set_slaac_network_config_for_testing(
        std::make_unique<NetworkConfig>(config));
  }

  brillo::VariantDictionary GetNetworkConfigOnService() {
    brillo::VariantDictionary props;
    Error error;
    service_->store().GetProperties(&props, &error);
    return props[kNetworkConfigProperty].Get<brillo::VariantDictionary>();
  }

  std::unique_ptr<Network> network_;
};

TEST_F(ServiceNetworkConfigTest, NonEmptyValues) {
  service_->AttachNetwork(network_->AsWeakPtr());

  NetworkConfig config;
  config.ipv4_address = *IPv4CIDR::CreateFromCIDRString(kIPv4Addr);
  config.ipv4_gateway = *IPv4Address::CreateFromString(kIPv4Gateway);
  config.dns_servers = {
      *IPAddress::CreateFromString(kDNS1),
      *IPAddress::CreateFromString(kDNS2),
      *IPAddress::CreateFromString(kDNS3),
  };
  config.dns_search_domains = {kSearchDomain1, kSearchDomain2};
  config.included_route_prefixes = {
      *IPCIDR::CreateFromCIDRString(kIncludedRoute1),
      *IPCIDR::CreateFromCIDRString(kIncludedRoute2),
  };
  config.excluded_route_prefixes = {
      *IPCIDR::CreateFromCIDRString(kExcludedRoute1),
      *IPCIDR::CreateFromCIDRString(kExcludedRoute2),
  };
  config.mtu = kMTU;
  SetLinkProtocolNetworkConfigOnNetwork(config);

  NetworkConfig slaac_config;
  slaac_config.ipv6_addresses = {
      *IPv6CIDR::CreateFromCIDRString(kIPv6Addr1),
      *IPv6CIDR::CreateFromCIDRString(kIPv6Addr2),
  };
  slaac_config.ipv6_gateway = *IPv6Address::CreateFromString(kIPv6Gateway);
  slaac_config.pref64 = net_base::IPv6CIDR::CreateFromCIDRString(kPref64);
  SetSLAACNetworkConfigOnNetwork(slaac_config);

  brillo::VariantDictionary actual = GetNetworkConfigOnService();

  EXPECT_EQ(actual[kNetworkConfigIPv4AddressProperty].Get<std::string>(),
            kIPv4Addr);
  EXPECT_EQ(actual[kNetworkConfigIPv4GatewayProperty].Get<std::string>(),
            kIPv4Gateway);
  EXPECT_EQ(actual[kNetworkConfigIPv6AddressesProperty]
                .Get<std::vector<std::string>>(),
            std::vector<std::string>({kIPv6Addr1, kIPv6Addr2}));
  EXPECT_EQ(actual[kNetworkConfigIPv6GatewayProperty].Get<std::string>(),
            kIPv6Gateway);
  EXPECT_EQ(
      actual[kNetworkConfigNameServersProperty].Get<std::vector<std::string>>(),
      std::vector<std::string>({kDNS1, kDNS2, kDNS3}));
  EXPECT_EQ(actual[kNetworkConfigSearchDomainsProperty]
                .Get<std::vector<std::string>>(),
            std::vector<std::string>({kSearchDomain1, kSearchDomain2}));
  EXPECT_EQ(actual[kNetworkConfigIncludedRoutesProperty],
            std::vector<std::string>({kIncludedRoute1, kIncludedRoute2}));
  EXPECT_EQ(actual[kNetworkConfigExcludedRoutesProperty],
            std::vector<std::string>({kExcludedRoute1, kExcludedRoute2}));
  EXPECT_EQ(actual[kNetworkConfigMTUProperty], kMTU);
  EXPECT_EQ(actual[kNetworkConfigPref64Property].Get<std::string>(), kPref64);
}

TEST_F(ServiceNetworkConfigTest, DefaultValues) {
  service_->AttachNetwork(network_->AsWeakPtr());

  brillo::VariantDictionary actual = GetNetworkConfigOnService();

  EXPECT_EQ(actual[kNetworkConfigIPv4AddressProperty].Get<std::string>(), "");
  EXPECT_EQ(actual[kNetworkConfigIPv4GatewayProperty].Get<std::string>(), "");
  EXPECT_EQ(actual[kNetworkConfigIPv6AddressesProperty]
                .Get<std::vector<std::string>>(),
            std::vector<std::string>());
  EXPECT_EQ(actual[kNetworkConfigIPv6GatewayProperty].Get<std::string>(), "");
  EXPECT_EQ(
      actual[kNetworkConfigNameServersProperty].Get<std::vector<std::string>>(),
      std::vector<std::string>());
  EXPECT_EQ(actual[kNetworkConfigSearchDomainsProperty]
                .Get<std::vector<std::string>>(),
            std::vector<std::string>());
  EXPECT_EQ(actual[kNetworkConfigIncludedRoutesProperty],
            std::vector<std::string>());
  EXPECT_EQ(actual[kNetworkConfigExcludedRoutesProperty],
            std::vector<std::string>());
  EXPECT_EQ(actual[kNetworkConfigMTUProperty], 0);
}

TEST_F(ServiceNetworkConfigTest, EmptyNetworkConfig) {
  brillo::VariantDictionary actual = GetNetworkConfigOnService();
  EXPECT_EQ(0, actual.size());
}

TEST_F(ServiceNetworkConfigTest, EmitChangeOnNetworkConfigChanges) {
  service_->AttachNetwork(network_->AsWeakPtr());
  EXPECT_CALL(*GetAdaptor(),
              EmitKeyValueStoreChanged(kNetworkConfigProperty, _));
  service_->network_event_handler()->OnIPConfigsPropertyUpdated(
      /*interface_index=*/1);
}

TEST_F(ServiceNetworkConfigTest, EmitChangeOnAttachNetwork) {
  EXPECT_CALL(*GetAdaptor(),
              EmitKeyValueStoreChanged(kNetworkConfigProperty, _));
  service_->AttachNetwork(network_->AsWeakPtr());
}

TEST_F(ServiceNetworkConfigTest, EmitChangeOnDetachNetwork) {
  service_->AttachNetwork(network_->AsWeakPtr());
  EXPECT_CALL(*GetAdaptor(),
              EmitKeyValueStoreChanged(kNetworkConfigProperty, _));
  service_->AttachNetwork(nullptr);
}

// Verifies that the NetworkConfig change will toggle the EnableRFC8925 flag on
// Service properly.
TEST_F(ServiceNetworkConfigTest, ChangeEnableRFC8925Flag) {
  service_->AttachNetwork(network_->AsWeakPtr());
  const auto kIPv6LinkLocalAddr = *IPAddress::CreateFromString("fe80::1");
  const auto kIPv6NonLinkLocalAddr = *IPAddress::CreateFromString("fd01::1");
  const auto kIPv4Addr = *IPAddress::CreateFromString("8.8.8.8");

  // Default should be false.
  ASSERT_FALSE(service_->enable_rfc_8925());

  struct TestCase {
    std::vector<IPAddress> dns_list;
    std::optional<bool> expected_flag_value;  // nullopt means do not change.
  } test_cases[] = {
      {{}, std::nullopt},
      {{kIPv4Addr}, std::nullopt},
      {{kIPv6LinkLocalAddr}, false},
      {{kIPv6NonLinkLocalAddr}, true},
      {{kIPv4Addr, kIPv6LinkLocalAddr}, false},
      {{kIPv4Addr, kIPv6NonLinkLocalAddr}, true},
      {{kIPv6LinkLocalAddr, kIPv6NonLinkLocalAddr}, true},
  };

  const auto describe_test_case = [](const TestCase& tc) -> std::string {
    std::vector<std::string> dns_strs;
    for (const auto ip : tc.dns_list) {
      dns_strs.push_back(ip.ToString());
    }
    return base::StringPrintf("dns={%s}",
                              base::JoinString(dns_strs, ", ").c_str());
  };

  const auto trigger_dns_update =
      [this](const std::vector<net_base::IPAddress>& dns_list) {
        NetworkConfig config;
        config.dns_servers = dns_list;
        SetLinkProtocolNetworkConfigOnNetwork(config);
        service_->network_event_handler()->OnIPConfigsPropertyUpdated(
            /*interface_index=*/1);
      };

  for (const auto& tc : test_cases) {
    // Set the initial flag value to false.
    const bool init_value = false;
    trigger_dns_update({kIPv6LinkLocalAddr});
    ASSERT_EQ(service_->enable_rfc_8925(), init_value);

    trigger_dns_update(tc.dns_list);
    ASSERT_EQ(service_->enable_rfc_8925(),
              tc.expected_flag_value.value_or(init_value))
        << describe_test_case(tc);
  }

  for (const auto& tc : test_cases) {
    // Set the initial flag value to false.
    const bool init_value = true;
    trigger_dns_update({kIPv6NonLinkLocalAddr});
    ASSERT_EQ(service_->enable_rfc_8925(), init_value);

    trigger_dns_update(tc.dns_list);
    ASSERT_EQ(service_->enable_rfc_8925(),
              tc.expected_flag_value.value_or(init_value))
        << describe_test_case(tc);
  }
}

// Verifies that the EnableRFC8925 flag value can be persisted into and loaded
// from the storage.
TEST_F(ServiceNetworkConfigTest, SaveLoadEnableRFC8925Flag) {
  FakeStore storage;
  service_->AttachNetwork(network_->AsWeakPtr());
  const auto kIPv6LinkLocalAddr = *IPAddress::CreateFromString("fe80::1");
  const auto kIPv6NonLinkLocalAddr = *IPAddress::CreateFromString("fd01::1");

  const auto update_enable_rfc_8925_flag = [&, this](bool enable) {
    NetworkConfig config;
    if (enable) {
      config.dns_servers = {kIPv6NonLinkLocalAddr};
    } else {
      config.dns_servers = {kIPv6LinkLocalAddr};
    }
    SetLinkProtocolNetworkConfigOnNetwork(config);
    service_->network_event_handler()->OnIPConfigsPropertyUpdated(
        /*interface_index=*/1);
  };

  // Save and load the value directly.
  update_enable_rfc_8925_flag(true);
  ASSERT_TRUE(service_->enable_rfc_8925());
  ASSERT_TRUE(service_->Save(&storage));
  ASSERT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(service_->enable_rfc_8925());

  // Change the value and load again.
  update_enable_rfc_8925_flag(false);
  ASSERT_FALSE(service_->enable_rfc_8925());
  ASSERT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(service_->enable_rfc_8925());
}

}  // namespace shill
