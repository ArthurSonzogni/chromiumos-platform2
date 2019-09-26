// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "shill/dhcp/mock_dhcp_properties.h"
#include "shill/ethernet/ethernet_service.h"
#include "shill/event_dispatcher.h"
#include "shill/manager.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_power_manager.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
#include "shill/net/mock_time.h"
#include "shill/property_store_test.h"
#include "shill/service_property_change_test.h"
#include "shill/service_under_test.h"
#include "shill/testing.h"

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
#include "shill/mock_eap_credentials.h"
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

using base::Bind;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DefaultValue;
using testing::DoAll;
using testing::HasSubstr;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNull;
using testing::ReturnRef;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Test;
using testing::Values;

namespace {
const char kConnectDisconnectReason[] = "RPC";
}

namespace shill {

class ServiceTest : public PropertyStoreTest {
 public:
  ServiceTest()
      : mock_manager_(control_interface(), dispatcher(), metrics()),
        service_(new ServiceUnderTest(&mock_manager_)),
        service2_(new ServiceUnderTest(&mock_manager_)),
        storage_id_(ServiceUnderTest::kStorageId),
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
        eap_(new MockEapCredentials()),
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
        power_manager_(new MockPowerManager(control_interface())) {
    ON_CALL(*control_interface(), CreatePowerManagerProxy(_, _, _))
        .WillByDefault(ReturnNull());

    service_->disconnects_.time_ = &time_;
    service_->misconnects_.time_ = &time_;
    DefaultValue<Timestamp>::Set(Timestamp());
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
    service_->eap_.reset(eap_);  // Passes ownership.
#endif                           // DISABLE_WIFI || DISABLE_WIRED_8021X
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

  string GetFriendlyName() { return service_->friendly_name(); }

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

  void NoteFailureEvent() { service_->NoteFailureEvent(); }

  EventHistory* GetDisconnects() { return &service_->disconnects_; }
  EventHistory* GetMisconnects() { return &service_->misconnects_; }

  Timestamp GetTimestamp(int monotonic_seconds,
                         int boottime_seconds,
                         const string& wall_clock) {
    struct timeval monotonic = {.tv_sec = monotonic_seconds, .tv_usec = 0};
    struct timeval boottime = {.tv_sec = boottime_seconds, .tv_usec = 0};
    return Timestamp(monotonic, boottime, wall_clock);
  }

  void PushTimestamp(EventHistory* events,
                     int monotonic_seconds,
                     int boottime_seconds,
                     const string& wall_clock) {
    events->RecordEventInternal(
        GetTimestamp(monotonic_seconds, boottime_seconds, wall_clock));
  }

  int GetDisconnectsMonitorSeconds() {
    return Service::kDisconnectsMonitorSeconds;
  }

  int GetMisconnectsMonitorSeconds() {
    return Service::kMisconnectsMonitorSeconds;
  }

  int GetMaxDisconnectEventHistory() {
    return Service::kMaxDisconnectEventHistory;
  }

  int GetMaxMisconnectEventHistory() {
    return Service::kMaxMisconnectEventHistory;
  }

  bool GetAutoConnect(Error* error) { return service_->GetAutoConnect(error); }

  void ClearAutoConnect(Error* error) { service_->ClearAutoConnect(error); }

  bool SetAutoConnectFull(bool connect, Error* error) {
    return service_->SetAutoConnectFull(connect, error);
  }

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

  MockManager mock_manager_;
  MockTime time_;
  scoped_refptr<ServiceUnderTest> service_;
  scoped_refptr<ServiceUnderTest> service2_;
  string storage_id_;
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  MockEapCredentials* eap_;          // Owned by |service_|.
#endif                               // DISABLE_WIFI || DISABLE_WIRED_8021X
  MockPowerManager* power_manager_;  // Owned by |mock_manager_|.
  vector<Technology> technology_order_for_sorting_;
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
  EXPECT_EQ(Service::kCheckPortalAuto, service_->check_portal_);
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
  service_->technology_ = Technology::kWifi;
  Error error;
  EXPECT_EQ(kTypeWifi, service_->CalculateTechnology(&error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, GetProperties) {
  {
    brillo::VariantDictionary props;
    Error error;
    string expected("true");
    service_->mutable_store()->SetStringProperty(kCheckPortalProperty, expected,
                                                 &error);
    EXPECT_TRUE(service_->store().GetProperties(&props, &error));
    ASSERT_FALSE(props.find(kCheckPortalProperty) == props.end());
    EXPECT_TRUE(props[kCheckPortalProperty].IsTypeCompatible<string>());
    EXPECT_EQ(props[kCheckPortalProperty].Get<string>(), expected);
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
    EXPECT_EQ(props[kDeviceProperty].Get<dbus::ObjectPath>().value(),
              ServiceUnderTest::kRpcId);
  }
}

TEST_F(ServiceTest, SetProperty) {
  {
    Error error;
    EXPECT_TRUE(service_->mutable_store()->SetAnyProperty(
        kSaveCredentialsProperty, PropertyStoreTest::kBoolV, &error));
  }
  {
    Error error;
    const int32_t priority = 1;
    EXPECT_TRUE(service_->mutable_store()->SetAnyProperty(
        kPriorityProperty, brillo::Any(priority), &error));
  }
  {
    Error error;
    const string guid("not default");
    EXPECT_TRUE(service_->mutable_store()->SetAnyProperty(
        kGuidProperty, brillo::Any(guid), &error));
  }
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  // Ensure that EAP properties cannot be set on services with no EAP
  // credentials.  Use service2_ here since we're have some code in
  // ServiceTest::SetUp() that fiddles with service_->eap_.
  {
    Error error;
    string eap("eap eep eip!");
    EXPECT_FALSE(service2_->mutable_store()->SetAnyProperty(
        kEapMethodProperty, brillo::Any(eap), &error));
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidProperty, error.type());
    // Now plumb in eap credentials, and try again.
    service2_->SetEapCredentials(new EapCredentials());
    EXPECT_TRUE(service2_->mutable_store()->SetAnyProperty(
        kEapMethodProperty, brillo::Any(eap), &error));
  }
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  {
    Error error;
    EXPECT_FALSE(service_->mutable_store()->SetAnyProperty(
        kConnectableProperty, PropertyStoreTest::kBoolV, &error));
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  {
    bool auto_connect = true;
    Error error;
    EXPECT_TRUE(service_->mutable_store()->SetAnyProperty(
        kAutoConnectProperty, brillo::Any(auto_connect), &error));
  }
  // Ensure that we can perform a trivial set of the Name property (to its
  // current value) but an attempt to set the property to a different value
  // fails.
  {
    Error error;
    EXPECT_FALSE(service_->mutable_store()->SetAnyProperty(
        kNameProperty, brillo::Any(GetFriendlyName()), &error));
    EXPECT_FALSE(error.IsFailure());
  }
  {
    Error error;
    EXPECT_FALSE(service_->mutable_store()->SetAnyProperty(
        kNameProperty, PropertyStoreTest::kStringV, &error));
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
}

TEST_F(ServiceTest, GetLoadableStorageIdentifier) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_EQ("", service_->GetLoadableStorageIdentifier(storage));
  EXPECT_EQ(storage_id_, service_->GetLoadableStorageIdentifier(storage));
}

TEST_F(ServiceTest, IsLoadableFrom) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(service_->IsLoadableFrom(storage));
  EXPECT_TRUE(service_->IsLoadableFrom(storage));
}

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
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
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

TEST_F(ServiceTest, Load) {
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  MockEapCredentials* eap = new MockEapCredentials();  // Owned by |service|.
  scoped_refptr<ServiceWithOnEapCredentialsChangedOverride> service(
      new ServiceWithOnEapCredentialsChangedOverride(&mock_manager_, eap));
#else
  scoped_refptr<ServiceUnderTest> service(new ServiceUnderTest(&mock_manager_));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillOnce(Return(true));
  const string kCheckPortal("check-portal");
  const string kGUID("guid");
  const bool kHasEverConnected = true;
  const int kPriority = 20;
  const string kProxyConfig("proxy-config");
  const string kUIData("ui-data");
  EXPECT_CALL(storage, GetString(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage, GetInt(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage, GetString(storage_id_, Service::kStorageCheckPortal, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kCheckPortal), Return(true)));
  EXPECT_CALL(storage, GetString(storage_id_, Service::kStorageGUID, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kGUID), Return(true)));
  EXPECT_CALL(storage, GetInt(storage_id_, Service::kStoragePriority, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kPriority), Return(true)));
  EXPECT_CALL(storage, GetString(storage_id_, Service::kStorageProxyConfig, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kProxyConfig), Return(true)));
  EXPECT_CALL(storage, GetString(storage_id_, Service::kStorageUIData, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kUIData), Return(true)));
  EXPECT_CALL(storage, GetBool(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage,
              GetBool(storage_id_, Service::kStorageSaveCredentials, _));
  EXPECT_CALL(storage,
              GetBool(storage_id_, Service::kStorageHasEverConnected, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kHasEverConnected), Return(true)));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap, Load(&storage, storage_id_));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  MockDhcpProperties* dhcp_props = new MockDhcpProperties();
  service->dhcp_properties_.reset(dhcp_props);
  EXPECT_CALL(*dhcp_props, Load(&storage, storage_id_));

  EXPECT_TRUE(service->Load(&storage));
  EXPECT_EQ(kCheckPortal, service->check_portal_);
  EXPECT_EQ(kGUID, service->guid_);
  EXPECT_TRUE(service->has_ever_connected_);
  EXPECT_EQ(kProxyConfig, service->proxy_config_);
  EXPECT_EQ(kUIData, service->ui_data_);

  Mock::VerifyAndClearExpectations(&storage);
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  Mock::VerifyAndClearExpectations(eap_);
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  Mock::VerifyAndClearExpectations(dhcp_props);

  // Assure that parameters are set to default if not available in the profile.
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillOnce(Return(true));
  EXPECT_CALL(storage, GetBool(storage_id_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(storage, GetString(storage_id_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(storage, GetInt(storage_id_, _, _)).WillRepeatedly(Return(false));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap, Load(&storage, storage_id_));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  EXPECT_CALL(*dhcp_props, Load(&storage, storage_id_));

  EXPECT_TRUE(service->Load(&storage));
  EXPECT_EQ(Service::kCheckPortalAuto, service_->check_portal_);
  EXPECT_EQ("", service->guid_);
  EXPECT_EQ("", service->proxy_config_);
  EXPECT_EQ("", service->ui_data_);

  // has_ever_connected_ flag will reset when EAP credential changes.
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_FALSE(service->has_ever_connected_);
#else
  EXPECT_TRUE(service->has_ever_connected_);
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
}

TEST_F(ServiceTest, LoadFail) {
  StrictMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillOnce(Return(false));
  EXPECT_FALSE(service_->Load(&storage));
}

TEST_F(ServiceTest, LoadAutoConnect) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, GetBool(storage_id_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(storage, GetString(storage_id_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(storage, GetInt(storage_id_, _, _)).WillRepeatedly(Return(false));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Load(&storage, storage_id_)).Times(AnyNumber());
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Load(&storage, storage_id_)).Times(AnyNumber());
  service_->dhcp_properties_ = std::move(dhcp_props);

  EXPECT_CALL(storage, GetBool(storage_id_, Service::kStorageAutoConnect, _))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));

  // AutoConnect is unset.
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(service_->retain_auto_connect());

  // AutoConnect is false.
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());

  // AutoConnect is true.
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(service_->auto_connect());
  EXPECT_TRUE(service_->retain_auto_connect());
}

TEST_F(ServiceTest, SaveString) {
  MockStore storage;
  static const char kKey[] = "test-key";
  static const char kData[] = "test-data";
  EXPECT_CALL(storage, SetString(storage_id_, kKey, kData))
      .WillOnce(Return(true));
  service_->SaveString(&storage, storage_id_, kKey, kData, false, true);
}

TEST_F(ServiceTest, SaveStringCrypted) {
  MockStore storage;
  static const char kKey[] = "test-key";
  static const char kData[] = "test-data";
  EXPECT_CALL(storage, SetCryptedString(storage_id_, kKey, kData))
      .WillOnce(Return(true));
  service_->SaveString(&storage, storage_id_, kKey, kData, true, true);
}

TEST_F(ServiceTest, SaveStringDontSave) {
  MockStore storage;
  static const char kKey[] = "test-key";
  EXPECT_CALL(storage, DeleteKey(storage_id_, kKey)).WillOnce(Return(true));
  service_->SaveString(&storage, storage_id_, kKey, "data", false, false);
}

TEST_F(ServiceTest, SaveStringEmpty) {
  MockStore storage;
  static const char kKey[] = "test-key";
  EXPECT_CALL(storage, DeleteKey(storage_id_, kKey)).WillOnce(Return(true));
  service_->SaveString(&storage, storage_id_, kKey, "", true, true);
}

TEST_F(ServiceTest, Save) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, SetString(storage_id_, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage, DeleteKey(storage_id_, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage, DeleteKey(storage_id_, Service::kStorageAutoConnect))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetBool(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage, SetBool(storage_id_, Service::kStorageSaveCredentials,
                               service_->save_credentials()));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Save(&storage, storage_id_, true));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Save(&storage, storage_id_));
  service_->dhcp_properties_ = std::move(dhcp_props);
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(ServiceTest, RetainAutoConnect) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, SetString(storage_id_, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage, DeleteKey(storage_id_, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage, DeleteKey(storage_id_, Service::kStorageAutoConnect))
      .Times(0);
  EXPECT_CALL(storage, SetBool(storage_id_, _, _)).Times(AnyNumber());
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Save(&storage, storage_id_, true)).Times(2);
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

  // AutoConnect flag set true.
  service_->EnableAndRetainAutoConnect();
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageAutoConnect, true));
  EXPECT_TRUE(service_->Save(&storage));

  // AutoConnect flag set false.
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageAutoConnect, false));
  service_->SetAutoConnect(false);
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(ServiceTest, HasEverConnectedSavedToProfile) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, SetString(storage_id_, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage, DeleteKey(storage_id_, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(storage,
              DeleteKey(storage_id_, Service::kStorageHasEverConnected))
      .Times(0);
  EXPECT_CALL(storage, SetBool(storage_id_, _, _)).Times(AnyNumber());
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Save(&storage, storage_id_, true)).Times(2);
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

  // HasEverConnected flag set true.
  service_->SetHasEverConnected(true);
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageHasEverConnected, true));
  EXPECT_TRUE(service_->Save(&storage));

  // HasEverConnected flag set false.
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageHasEverConnected, false));
  service_->SetHasEverConnected(false);
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(ServiceTest, Unload) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillOnce(Return(true));
  static const string string_value("value");
  EXPECT_CALL(storage, GetString(storage_id_, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(DoAll(SetArgPointee<2>(string_value), Return(true)));
  EXPECT_CALL(storage, GetBool(storage_id_, _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_FALSE(service_->explicitly_disconnected_);
  service_->explicitly_disconnected_ = true;
  EXPECT_FALSE(service_->has_ever_connected_);
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Load(&storage, storage_id_));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  ASSERT_TRUE(service_->Load(&storage));
  // TODO(pstew): Only two string properties in the service are tested as
  // a sentinel that properties are being set and reset at the right times.
  // However, since property load/store is essentially a manual process,
  // it is error prone and should either be exhaustively unit-tested or
  // a generic framework for registering loaded/stored properties should
  // be created. crbug.com/207798
  EXPECT_EQ(string_value, service_->ui_data_);
  EXPECT_EQ(string_value, service_->guid_);
  EXPECT_FALSE(service_->explicitly_disconnected_);
  EXPECT_TRUE(service_->has_ever_connected_);
  service_->explicitly_disconnected_ = true;
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Reset());
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
  service_->Unload();
  EXPECT_EQ(string(""), service_->ui_data_);
  EXPECT_EQ(string(""), service_->guid_);
  EXPECT_FALSE(service_->explicitly_disconnected_);
  EXPECT_FALSE(service_->has_ever_connected_);
}

TEST_F(ServiceTest, State) {
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  EXPECT_EQ(Service::kFailureNone, service_->failure());
  const string no_error(Service::ConnectFailureToString(Service::kFailureNone));
  EXPECT_EQ(no_error, service_->error());

  EXPECT_CALL(*GetAdaptor(), EmitStringChanged(kStateProperty, _)).Times(6);
  EXPECT_CALL(*GetAdaptor(), EmitStringChanged(kErrorProperty, _)).Times(4);
  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  // A second state change shouldn't cause another update
  service_->SetState(Service::kStateConnected);
  EXPECT_EQ(Service::kStateConnected, service_->state());
  EXPECT_EQ(Service::kStateIdle, GetPreviousState());
  EXPECT_EQ(Service::kFailureNone, service_->failure());
  EXPECT_TRUE(service_->has_ever_connected_);

  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  service_->SetFailure(Service::kFailureOutOfRange);
  EXPECT_TRUE(service_->IsFailed());
  EXPECT_GT(service_->failed_time_, 0);
  EXPECT_GT(service_->previous_error_serial_number_, 0);
  EXPECT_EQ(Service::kStateFailure, service_->state());
  EXPECT_EQ(Service::kFailureOutOfRange, service_->failure());
  const string out_of_range_error(
      Service::ConnectFailureToString(Service::kFailureOutOfRange));
  EXPECT_EQ(out_of_range_error, service_->error());
  EXPECT_EQ(out_of_range_error, service_->previous_error_);

  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_FALSE(service_->IsFailed());
  EXPECT_EQ(service_->failed_time_, 0);
  EXPECT_EQ(no_error, service_->error());
  EXPECT_EQ(out_of_range_error, service_->previous_error_);
  EXPECT_GT(service_->previous_error_serial_number_, 0);

  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  service_->SetFailureSilent(Service::kFailurePinMissing);
  EXPECT_TRUE(service_->IsFailed());
  EXPECT_GT(service_->failed_time_, 0);
  EXPECT_GT(service_->previous_error_serial_number_, 0);
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_EQ(Service::kFailurePinMissing, service_->failure());
  const string pin_missing_error(
      Service::ConnectFailureToString(Service::kFailurePinMissing));
  EXPECT_EQ(pin_missing_error, service_->error());
  EXPECT_EQ(pin_missing_error, service_->previous_error_);

  // If the Service has a Profile, the profile should be saved when
  // the service enters kStateConnected. (The case where the service
  // doesn't have a profile is tested above.)
  MockProfileRefPtr mock_profile(new MockProfile(&mock_manager_));
  NiceMock<MockStore> storage;
  service_->set_profile(mock_profile);
  service_->has_ever_connected_ = false;
  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(&storage));
  EXPECT_CALL(*mock_profile, UpdateService(IsRefPtrTo(service_)));
  service_->SetState(Service::kStateConnected);
  EXPECT_TRUE(service_->has_ever_connected_);
  service_->set_profile(nullptr);  // Break reference cycle.

  // Similar to the above, but emulate an emphemeral profile, which
  // has no storage. We can't update the service in the profile, but
  // we should not crash.
  service_->state_ = Service::kStateIdle;  // Skips state change logic.
  service_->set_profile(mock_profile);
  service_->has_ever_connected_ = false;
  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(nullptr));
  service_->SetState(Service::kStateConnected);
  EXPECT_TRUE(service_->has_ever_connected_);
  service_->set_profile(nullptr);  // Break reference cycle.
}

TEST_F(ServiceTest, PortalDetectionFailure) {
  EXPECT_CALL(*GetAdaptor(),
              EmitStringChanged(kPortalDetectionFailedPhaseProperty,
                                kPortalDetectionPhaseDns))
      .Times(1);
  EXPECT_CALL(*GetAdaptor(),
              EmitStringChanged(kPortalDetectionFailedStatusProperty,
                                kPortalDetectionStatusTimeout))
      .Times(1);
  service_->SetPortalDetectionFailure(kPortalDetectionPhaseDns,
                                      kPortalDetectionStatusTimeout);
  EXPECT_EQ(kPortalDetectionPhaseDns,
            service_->portal_detection_failure_phase_);
  EXPECT_EQ(kPortalDetectionStatusTimeout,
            service_->portal_detection_failure_status_);
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

TEST_F(ServiceTest, UserInitiatedConnectionResult) {
  service_->technology_ = Technology::kWifi;
  Error error;
  // User-initiated connection attempt succeed.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(
                              Metrics::kMetricWifiUserInitiatedConnectionResult,
                              Metrics::kUserInitiatedConnectionResultSuccess));
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionFailureReason(_, _))
      .Times(0);
  service_->SetState(Service::kStateConnected);
  Mock::VerifyAndClearExpectations(metrics());

  // User-initiated connection attempt failed.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(
                              Metrics::kMetricWifiUserInitiatedConnectionResult,
                              Metrics::kUserInitiatedConnectionResultFailure));
  EXPECT_CALL(*metrics(),
              NotifyUserInitiatedConnectionFailureReason(
                  Metrics::kMetricWifiUserInitiatedConnectionFailureReason,
                  Service::kFailureDHCP));
  service_->SetFailure(Service::kFailureDHCP);
  Mock::VerifyAndClearExpectations(metrics());

  // User-initiated connection attempt aborted.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  service_->SetState(Service::kStateAssociating);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(
                              Metrics::kMetricWifiUserInitiatedConnectionResult,
                              Metrics::kUserInitiatedConnectionResultAborted));
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionFailureReason(_, _))
      .Times(0);
  service_->SetState(Service::kStateIdle);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for other state transition.
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(_, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionFailureReason(_, _))
      .Times(0);
  service_->SetState(Service::kStateAssociating);
  service_->SetState(Service::kStateConfiguring);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for non-user-initiated connection.
  service_->SetState(Service::kStateIdle);
  service_->Connect(&error, "in test");
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(_, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionFailureReason(_, _))
      .Times(0);
  service_->SetState(Service::kStateConnected);
  Mock::VerifyAndClearExpectations(metrics());

  // No metric reporting for other technology.
  service_->technology_ = Technology::kCellular;
  service_->SetState(Service::kStateIdle);
  service_->UserInitiatedConnect(kConnectDisconnectReason, &error);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionResult(_, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyUserInitiatedConnectionFailureReason(_, _))
      .Times(0);
  service_->SetFailure(Service::kFailureDHCP);
  Mock::VerifyAndClearExpectations(metrics());
}

TEST_F(ServiceTest, ActivateCellularModem) {
  ResultCallback callback = Bind(&ServiceTest::TestCallback, Unretained(this));
  EXPECT_CALL(*this, TestCallback(_)).Times(0);
  Error error;
  service_->ActivateCellularModem("Carrier", &error, callback);
  EXPECT_TRUE(error.IsFailure());
}

TEST_F(ServiceTest, CompleteCellularActivation) {
  Error error;
  service_->CompleteCellularActivation(&error);
  EXPECT_EQ(Error::kNotSupported, error.type());
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
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(storage_id_)).WillOnce(Return(true));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  EXPECT_CALL(*eap_, Load(&storage, storage_id_));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
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
  EXPECT_STREQ(Service::kAutoConnTechnologyNotConnectable, reason);
}

TEST_F(ServiceTest, AutoConnectLogging) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _));
  service_->SetConnectable(true);

  ScopeLogger::GetInstance()->EnableScopesByName("+service");
  ScopeLogger::GetInstance()->set_verbose_level(1);
  service_->SetState(Service::kStateConnected);
  EXPECT_CALL(log, Log(-1, _, HasSubstr(Service::kAutoConnConnected)));
  service_->AutoConnect();

  ScopeLogger::GetInstance()->EnableScopesByName("-service");
  ScopeLogger::GetInstance()->set_verbose_level(0);
  EXPECT_CALL(log, Log(logging::LOG_INFO, _,
                       HasSubstr(Service::kAutoConnNotConnectable)));
  service_->SetConnectable(false);
  service_->AutoConnect();
}

TEST_F(AllMockServiceTest, AutoConnectWithFailures) {
  const char* reason;
  service_->SetConnectable(true);
  service_->technology_ = Technology::kEthernet;
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // The very first AutoConnect() doesn't trigger any throttling.
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, _)).Times(0);
  service_->AutoConnect();
  Mock::VerifyAndClearExpectations(&dispatcher_);
  EXPECT_TRUE(service_->IsAutoConnectable(&reason));

  // The second call does trigger some throttling.
  EXPECT_CALL(
      dispatcher_,
      PostDelayedTask(_, _, Service::kMinAutoConnectCooldownTimeMilliseconds));
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
  uint64_t next_cooldown_time = service_->auto_connect_cooldown_milliseconds_;
  EXPECT_EQ(next_cooldown_time,
            Service::kAutoConnectCooldownBackoffFactor *
                Service::kMinAutoConnectCooldownTimeMilliseconds);
  while (next_cooldown_time <=
         service_->GetMaxAutoConnectCooldownTimeMilliseconds()) {
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
        PostDelayedTask(_, _,
                        service_->GetMaxAutoConnectCooldownTimeMilliseconds()));
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
  EXPECT_EQ(service_->auto_connect_cooldown_milliseconds_, 0);

  // But future AutoConnects behave as before
  EXPECT_CALL(
      dispatcher_,
      PostDelayedTask(_, _, Service::kMinAutoConnectCooldownTimeMilliseconds))
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

TEST_F(ServiceTest, ConfigureBadProperty) {
  KeyValueStore args;
  args.SetString("XXXInvalid", "Value");
  Error error;
  service_->Configure(args, &error);
  EXPECT_FALSE(error.IsSuccess());
}

TEST_F(ServiceTest, ConfigureBoolProperty) {
  service_->EnableAndRetainAutoConnect();
  service_->SetAutoConnect(false);
  ASSERT_FALSE(service_->auto_connect());
  KeyValueStore args;
  args.SetBool(kAutoConnectProperty, true);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(service_->auto_connect());
}

TEST_F(ServiceTest, ConfigureStringProperty) {
  const string kGuid0 = "guid_zero";
  const string kGuid1 = "guid_one";
  service_->SetGuid(kGuid0, nullptr);
  ASSERT_EQ(kGuid0, service_->guid());
  KeyValueStore args;
  args.SetString(kGuidProperty, kGuid1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kGuid1, service_->guid());
}

TEST_F(ServiceTest, ConfigureStringsProperty) {
  const vector<string> kStrings0{"string0", "string1"};
  const vector<string> kStrings1{"string2", "string3"};
  service_->set_strings(kStrings0);
  ASSERT_EQ(kStrings0, service_->strings());
  KeyValueStore args;
  args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kStrings1, service_->strings());
}

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
TEST_F(ServiceTest, ConfigureEapStringProperty) {
  MockEapCredentials* eap = new MockEapCredentials();
  service2_->SetEapCredentials(eap);  // Passes ownership.

  const string kEAPManagement0 = "management_zero";
  const string kEAPManagement1 = "management_one";
  service2_->SetEAPKeyManagement(kEAPManagement0);

  EXPECT_CALL(*eap, key_management()).WillOnce(ReturnRef(kEAPManagement0));
  ASSERT_EQ(kEAPManagement0, service2_->GetEAPKeyManagement());
  KeyValueStore args;
  EXPECT_CALL(*eap, SetKeyManagement(kEAPManagement1, _));
  args.SetString(kEapKeyMgmtProperty, kEAPManagement1);
  Error error;
  service2_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

TEST_F(ServiceTest, ConfigureIntProperty) {
  const int kPriority0 = 100;
  const int kPriority1 = 200;
  service_->SetPriority(kPriority0, nullptr);
  ASSERT_EQ(kPriority0, service_->priority());
  KeyValueStore args;
  args.SetInt(kPriorityProperty, kPriority1);
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
  args.SetBool(kAutoConnectProperty, true);
  Error error;
  service_->IgnoreParameterForConfigure(kAutoConnectProperty);
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->auto_connect());
}

TEST_F(ServiceTest, ConfigureProfileProperty) {
  // Ensure that the Profile property is always ignored.
  KeyValueStore args;
  args.SetString(kProfileProperty, "profile");
  Error error;
  EXPECT_CALL(mock_manager_, SetProfileForService(_, _, _)).Times(0);
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ServiceTest, ConfigureKeyValueStoreProperty) {
  KeyValueStore key_value_store0;
  key_value_store0.SetBool("key0", true);
  KeyValueStore key_value_store1;
  key_value_store1.SetInt("key1", 1);
  service_->SetKeyValueStore(key_value_store0, nullptr);
  ASSERT_EQ(key_value_store0, service_->GetKeyValueStore(nullptr));
  KeyValueStore args;
  args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                        key_value_store1);
  Error error;
  service_->Configure(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(key_value_store1, service_->GetKeyValueStore(nullptr));
}

TEST_F(ServiceTest, DoPropertiesMatch) {
  service_->SetAutoConnect(false);
  const string kGUID0 = "guid_zero";
  const string kGUID1 = "guid_one";
  service_->SetGuid(kGUID0, nullptr);
  const uint32_t kPriority0 = 100;
  const uint32_t kPriority1 = 200;
  service_->SetPriority(kPriority0, nullptr);
  const vector<string> kStrings0{"string0", "string1"};
  const vector<string> kStrings1{"string2", "string3"};
  service_->set_strings(kStrings0);
  KeyValueStore key_value_store0;
  key_value_store0.SetBool("key0", true);
  KeyValueStore key_value_store1;
  key_value_store1.SetInt("key1", 1);
  service_->SetKeyValueStore(key_value_store0, nullptr);

  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID0);
    args.SetBool(kAutoConnectProperty, false);
    args.SetInt(kPriorityProperty, kPriority0);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings0);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store0);
    EXPECT_TRUE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID1);
    args.SetBool(kAutoConnectProperty, false);
    args.SetInt(kPriorityProperty, kPriority0);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings0);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID0);
    args.SetBool(kAutoConnectProperty, true);
    args.SetInt(kPriorityProperty, kPriority0);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings0);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID0);
    args.SetBool(kAutoConnectProperty, false);
    args.SetInt(kPriorityProperty, kPriority1);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings0);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID0);
    args.SetBool(kAutoConnectProperty, false);
    args.SetInt(kPriorityProperty, kPriority0);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings1);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
                          key_value_store0);
    EXPECT_FALSE(service_->DoPropertiesMatch(args));
  }
  {
    KeyValueStore args;
    args.SetString(kGuidProperty, kGUID0);
    args.SetBool(kAutoConnectProperty, false);
    args.SetInt(kPriorityProperty, kPriority0);
    args.SetStrings(ServiceUnderTest::kStringsProperty, kStrings0);
    args.SetKeyValueStore(ServiceUnderTest::kKeyValueStoreProperty,
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
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(IsRefPtrTo(service_)))
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
  NiceMock<MockStore> storage;
  EXPECT_CALL(*profile, GetConstStorage()).WillOnce(Return(&storage));
  service_->OnPropertyChanged("");
}

TEST_F(ServiceTest, RecheckPortal) {
  service_->state_ = Service::kStateIdle;
  EXPECT_CALL(mock_manager_, RecheckPortalOnService(_)).Times(0);
  service_->OnPropertyChanged(kCheckPortalProperty);

  service_->state_ = Service::kStateNoConnectivity;
  EXPECT_CALL(mock_manager_, RecheckPortalOnService(IsRefPtrTo(service_)))
      .Times(1);
  service_->OnPropertyChanged(kCheckPortalProperty);

  service_->state_ = Service::kStateConnected;
  EXPECT_CALL(mock_manager_, RecheckPortalOnService(IsRefPtrTo(service_)))
      .Times(1);
  service_->OnPropertyChanged(kProxyConfigProperty);

  service_->state_ = Service::kStateOnline;
  EXPECT_CALL(mock_manager_, RecheckPortalOnService(IsRefPtrTo(service_)))
      .Times(1);
  service_->OnPropertyChanged(kCheckPortalProperty);

  service_->state_ = Service::kStateNoConnectivity;
  EXPECT_CALL(mock_manager_, RecheckPortalOnService(_)).Times(0);
  service_->OnPropertyChanged(kEapKeyIdProperty);
}

TEST_F(ServiceTest, SetCheckPortal) {
  {
    Error error;
    service_->SetCheckPortal("false", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::kCheckPortalFalse, service_->check_portal_);
  }
  {
    Error error;
    service_->SetCheckPortal("true", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::kCheckPortalTrue, service_->check_portal_);
  }
  {
    Error error;
    service_->SetCheckPortal("auto", &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(Service::kCheckPortalAuto, service_->check_portal_);
  }
  {
    Error error;
    service_->SetCheckPortal("xxx", &error);
    EXPECT_FALSE(error.IsSuccess());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
    EXPECT_EQ(Service::kCheckPortalAuto, service_->check_portal_);
  }
}

TEST_F(ServiceTest, SetFriendlyName) {
  EXPECT_EQ(service_->unique_name_, service_->friendly_name_);
  ServiceMockAdaptor* adaptor = GetAdaptor();

  EXPECT_CALL(*adaptor, EmitStringChanged(_, _)).Times(0);
  service_->SetFriendlyName(service_->unique_name_);
  EXPECT_EQ(service_->unique_name_, service_->friendly_name_);

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

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
class WriteOnlyServicePropertyTest : public ServiceTest {};
TEST_P(WriteOnlyServicePropertyTest, PropertyWriteOnly) {
  // Use a real EapCredentials instance since the base Service class
  // contains no write-only properties.
  EapCredentials eap;
  eap.InitPropertyStore(service_->mutable_store());

  string property(GetParam().Get<string>());
  Error error;
  EXPECT_FALSE(service_->store().GetStringProperty(property, nullptr, &error));
  EXPECT_EQ(Error::kPermissionDenied, error.type());
}

INSTANTIATE_TEST_CASE_P(WriteOnlyServicePropertyTestInstance,
                        WriteOnlyServicePropertyTest,
                        Values(brillo::Any(string(kEapPasswordProperty))));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

TEST_F(ServiceTest, GetIPConfigRpcIdentifier) {
  {
    Error error;
    EXPECT_EQ(control_interface()->NullRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }

  auto mock_device_info =
      std::make_unique<NiceMock<MockDeviceInfo>>(&mock_manager_);
  scoped_refptr<MockConnection> mock_connection(
      new NiceMock<MockConnection>(mock_device_info.get()));

  service_->connection_ = mock_connection;

  {
    Error error;
    const RpcIdentifier empty_rpcid;
    EXPECT_CALL(*mock_connection, ipconfig_rpc_identifier())
        .WillOnce(ReturnRef(empty_rpcid));
    EXPECT_EQ(control_interface()->NullRpcIdentifier(),
              service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }

  {
    Error error;
    const RpcIdentifier nonempty_rpcid("/ipconfig/path");
    EXPECT_CALL(*mock_connection, ipconfig_rpc_identifier())
        .WillOnce(ReturnRef(nonempty_rpcid));
    EXPECT_EQ(nonempty_rpcid, service_->GetIPConfigRpcIdentifier(&error));
    EXPECT_EQ(Error::kSuccess, error.type());
  }

  // Assure orderly destruction of the Connection before DeviceInfo.
  service_->connection_ = nullptr;
  mock_connection = nullptr;
  mock_device_info.reset();
}

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
class ServiceWithMockOnEapCredentialsChanged : public ServiceUnderTest {
 public:
  explicit ServiceWithMockOnEapCredentialsChanged(Manager* manager)
      : ServiceUnderTest(manager), is_8021x_(false) {}
  MOCK_METHOD(void,
              OnEapCredentialsChanged,
              (Service::UpdateCredentialsReason),
              (override));
  bool Is8021x() const override { return is_8021x_; }
  void set_is_8021x(bool is_8021x) { is_8021x_ = is_8021x; }

 private:
  bool is_8021x_;
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
  EXPECT_CALL(log,
              Log(logging::LOG_WARNING, _, HasSubstr("exceeds our maximum")))
      .Times(2);
  string kSubject("foo");
  EXPECT_FALSE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements));
  EXPECT_FALSE(service_->AddEAPCertification(
      kSubject, Service::kEAPMaxCertificationElements + 1));
  EXPECT_FALSE(service_->remote_certification_.size());
  Mock::VerifyAndClearExpectations(&log);

  EXPECT_CALL(log,
              Log(logging::LOG_INFO, _, HasSubstr("Received certification")))
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
  EXPECT_CALL(log,
              Log(logging::LOG_INFO, _, HasSubstr("Received certification")))
      .Times(1);
  EXPECT_TRUE(service_->AddEAPCertification(
      kSubject + "x", Service::kEAPMaxCertificationElements - 1));

  service_->ClearEAPCertification();
  EXPECT_TRUE(service_->remote_certification_.empty());
}
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

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
  const string kWallClock0 = "2012-12-09T12:41:22.234567-0800";
  const string kWallClock1 = "2012-12-31T23:59:59.345678-0800";
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
  NiceMock<MockStore> storage;
  service_->set_profile(mock_profile);
  service_->SetAutoConnect(true);

  EXPECT_CALL(*mock_profile, UpdateService(_));
  EXPECT_CALL(*mock_profile, GetConstStorage()).WillOnce(Return(&storage));
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(IsRefPtrTo(service_)))
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
  EXPECT_NE(service_->unique_name(), service2_->unique_name());
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
  Error error;
  EXPECT_EQ("", service_->GetTethering(&error));
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(ServiceTest, MeteredOverride) {
  Error error;
  service_->SetMeteredProperty(true, &error);
  EXPECT_TRUE(service_->IsMetered());

  service_->SetMeteredProperty(false, &error);
  EXPECT_FALSE(service_->IsMetered());
}

TEST_F(ServiceTest, SaveMeteredOverride) {
  NiceMock<MockStore> storage;
  // Newly created services should not have a metered override value
  // since that is set by the user, and should thus have no value
  // to save to the storage.
  EXPECT_CALL(storage, SetBool(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageMeteredOverride, _))
      .Times(0);
  EXPECT_TRUE(service_->Save(&storage));
  Mock::VerifyAndClearExpectations(&storage);

  Error error;
  EXPECT_CALL(storage, SetBool(storage_id_, _, _)).Times(AnyNumber());
  EXPECT_CALL(storage,
              SetBool(storage_id_, Service::kStorageMeteredOverride, true))
      .Times(1);
  service_->SetMeteredProperty(true, &error);
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(ServiceTest, IsNotMeteredByDefault) {
  EXPECT_FALSE(service_->IsMetered());
}

class ServiceWithMockOnPropertyChanged : public ServiceUnderTest {
 public:
  explicit ServiceWithMockOnPropertyChanged(Manager* manager)
      : ServiceUnderTest(manager) {}
  MOCK_METHOD(void, OnPropertyChanged, (const string&), (override));
};

TEST_F(ServiceTest, ConfigureServiceTriggersOnPropertyChanged) {
  auto service(
      make_scoped_refptr(new ServiceWithMockOnPropertyChanged(&mock_manager_)));
  KeyValueStore args;
  args.SetString(kUIDataProperty, "terpsichorean ejectamenta");
  args.SetBool(kSaveCredentialsProperty, false);

  // Calling Configure with different values from before triggers a single
  // OnPropertyChanged call per property.
  EXPECT_CALL(*service, OnPropertyChanged(kUIDataProperty)).Times(1);
  EXPECT_CALL(*service, OnPropertyChanged(kSaveCredentialsProperty)).Times(1);
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
  EXPECT_CALL(mock_manager_, UpdateService(IsRefPtrTo(service_)));
  service_->ClearExplicitlyDisconnected();
  Mock::VerifyAndClearExpectations(&mock_manager_);
  EXPECT_FALSE(GetExplicitlyDisconnected());
}

TEST_F(ServiceTest, Compare) {
  // Construct our Services so that the string comparison of
  // unique_name_ differs from the numerical comparison of
  // serial_number_.
  vector<scoped_refptr<MockService>> mock_services;
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
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(IsRefPtrTo(service2)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(IsRefPtrTo(service10)))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(DefaultSortingOrderIs(service10, service2));
  Mock::VerifyAndClearExpectations(&mock_manager_);

  // Prefer the service with the more recently applied profile if neither
  // service is ephemeral.
  EXPECT_CALL(mock_manager_, IsServiceEphemeral(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_manager_,
              IsProfileBefore(IsRefPtrTo(profile2), IsRefPtrTo(profile10)))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_manager_,
              IsProfileBefore(IsRefPtrTo(profile10), IsRefPtrTo(profile2)))
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
      .WillRepeatedly(Return((Technology::kWifi)));

  technology_order_for_sorting_ = {Technology::kEthernet, Technology::kWifi};
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
  EXPECT_CALL(*service2, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_TRUE(DefaultSortingOrderIs(service2, service10));

  // Connected preferred over connected-but-portalled.
  service10->SetConnectable(false);
  service2->SetConnectable(true);
  EXPECT_CALL(*service10, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*service10, IsConnected()).WillRepeatedly(Return(true));
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
      .WillRepeatedly(Return((Technology::kWifi)));

  // Confirm that ethernet service is sorted above wifi service.
  technology_order_for_sorting_ = {Technology::kEthernet, Technology::kWifi};
  EXPECT_TRUE(DefaultSortingOrderIs(ethernet_service, wifi_service));

  // Even making the wifi service managed doesn't change the network sorting
  // order.
  wifi_service->managed_credentials_ = true;
  EXPECT_TRUE(DefaultSortingOrderIs(ethernet_service, wifi_service));
}

TEST_F(ServiceTest, SanitizeStorageIdentifier) {
  EXPECT_EQ("", Service::SanitizeStorageIdentifier(""));

  for (int c = 0; c < 256; ++c) {
    std::string identifier(1, c);
    std::string sanitized_identifier = std::isalnum(c) ? identifier : "_";
    EXPECT_EQ(sanitized_identifier,
              Service::SanitizeStorageIdentifier(identifier));
  }

  EXPECT_EQ("service_1_2_3_2_Dummy_Net_",
            Service::SanitizeStorageIdentifier("service_1-2:3.2_Dummy^Net!"));
}

}  // namespace shill
