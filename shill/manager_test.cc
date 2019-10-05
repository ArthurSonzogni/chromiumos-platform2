// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/manager.h"

#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/adaptor_interfaces.h"
#include "shill/default_service_observer.h"
#include "shill/device_claimer.h"
#include "shill/ephemeral_profile.h"
#include "shill/error.h"
#include "shill/ethernet/mock_ethernet_provider.h"
#include "shill/fake_store.h"
#include "shill/geolocation_info.h"
#include "shill/key_value_store.h"
#include "shill/link_monitor.h"
#include "shill/logging.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_log.h"
#include "shill/mock_metrics.h"
#include "shill/mock_power_manager.h"
#include "shill/mock_profile.h"
#include "shill/mock_resolver.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
#include "shill/mock_throttler.h"
#include "shill/portal_detector.h"
#include "shill/property_store_test.h"
#include "shill/resolver.h"
#include "shill/service_under_test.h"
#include "shill/testing.h"
#include "shill/upstart/mock_upstart.h"

#if !defined(DISABLE_WIFI)
#include "shill/wifi/mock_wifi_provider.h"
#include "shill/wifi/mock_wifi_service.h"
#include "shill/wifi/wifi_service.h"
#endif  // DISABLE_WIFI

#if !defined(DISABLE_WIRED_8021X)
#include "shill/ethernet/mock_ethernet_eap_provider.h"
#endif  // DISABLE_WIRED_8021X

using base::Bind;
using base::FilePath;
using base::ScopedTempDir;
using base::Unretained;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace shill {
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::ContainerEq;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::WithArg;

class ManagerTest : public PropertyStoreTest {
 public:
  ManagerTest()
      : power_manager_(new MockPowerManager(control_interface())),
        device_info_(new NiceMock<MockDeviceInfo>(manager())),
        manager_adaptor_(new NiceMock<ManagerMockAdaptor>()),
        ethernet_provider_(new NiceMock<MockEthernetProvider>()),
#if !defined(DISABLE_WIRED_8021X)
        ethernet_eap_provider_(new NiceMock<MockEthernetEapProvider>()),
#endif  // DISABLE_WIRED_8021X
#if !defined(DISABLE_WIFI)
        wifi_provider_(new NiceMock<MockWiFiProvider>()),
#endif  // DISABLE_WIFI
        throttler_(new StrictMock<MockThrottler>()),
        upstart_(new NiceMock<MockUpstart>(control_interface())) {
    ON_CALL(*control_interface(), CreatePowerManagerProxy(_, _, _))
        .WillByDefault(ReturnNull());

    SetRunning(true);

    // Replace the manager's adaptor with a quieter one, and one
    // we can do EXPECT*() against.  Passes ownership.
    manager()->adaptor_.reset(manager_adaptor_);

    manager()->ethernet_provider_.reset(ethernet_provider_);

#if !defined(DISABLE_WIRED_8021X)
    // Replace the manager's Ethernet EAP provider with our mock.
    // Passes ownership.
    manager()->ethernet_eap_provider_.reset(ethernet_eap_provider_);
#endif  // DISABLE_WIRED_8021X

#if !defined(DISABLE_WIFI)
    // Replace the manager's WiFi provider with our mock.  Passes
    // ownership.
    manager()->wifi_provider_.reset(wifi_provider_);
#endif  // DISABLE_WIFI

    // Replace the manager's throttler with our mock.
    manager()->throttler_.reset(throttler_);

    // Update the manager's map from technology to provider.
    manager()->UpdateProviderMapping();

    // Replace the manager's upstart instance with our mock.  Passes
    // ownership.
    manager()->upstart_.reset(upstart_);
  }
  ~ManagerTest() override = default;

  void SetUp() override {
    mock_devices_.push_back(
        new NiceMock<MockDevice>(manager(), "null0", "addr0", 0));
    mock_devices_.push_back(
        new NiceMock<MockDevice>(manager(), "null1", "addr1", 1));
    mock_devices_.push_back(
        new NiceMock<MockDevice>(manager(), "null2", "addr2", 2));
    mock_devices_.push_back(
        new NiceMock<MockDevice>(manager(), "null3", "addr3", 3));
  }

  void TearDown() override { mock_devices_.clear(); }

  bool IsDeviceRegistered(const DeviceRefPtr& device, Technology tech) {
    auto devices = manager()->FilterByTechnology(tech);
    return (devices.size() == 1 && devices[0].get() == device.get());
  }
  bool ServiceOrderIs(ServiceRefPtr svc1, ServiceRefPtr svc2);

  void AdoptProfile(Manager* manager, ProfileRefPtr profile) {
    manager->profiles_.push_back(profile);
  }

  void SetRunning(bool running) { manager()->running_ = running; }

  ProfileRefPtr GetEphemeralProfile(Manager* manager) {
    return manager->ephemeral_profile_;
  }

  vector<ProfileRefPtr>& GetProfiles(Manager* manager) {
    return manager->profiles_;
  }

  Profile* CreateProfileForManager(Manager* manager) {
    Profile::Identifier id("rather", "irrelevant");
    auto storage = std::make_unique<FakeStore>();
    if (!storage->Open())
      return nullptr;
    Profile* profile(new Profile(manager, id, FilePath(), false));
    profile->SetStorageForTest(std::move(storage));
    return profile;  // Passes ownership of "profile".
  }

  bool CreateBackingStoreForService(ScopedTempDir* temp_dir,
                                    const string& user_identifier,
                                    const string& profile_identifier,
                                    const string& service_name) {
    std::unique_ptr<StoreInterface> store =
        CreateStore(Profile::GetFinalStoragePath(
            temp_dir->GetPath(),
            Profile::Identifier(user_identifier, profile_identifier)));
    return store->Open() &&
           store->SetString(service_name, "rather", "irrelevant") &&
           store->Close();
  }

  Error::Type TestCreateProfile(Manager* manager, const string& name) {
    Error error;
    RpcIdentifier path;
    manager->CreateProfile(name, &path, &error);
    return error.type();
  }

  Error::Type TestPopAnyProfile(Manager* manager) {
    Error error;
    manager->PopAnyProfile(&error);
    return error.type();
  }

  Error::Type TestPopAllUserProfiles(Manager* manager) {
    Error error;
    manager->PopAllUserProfiles(&error);
    return error.type();
  }

  Error::Type TestPopProfile(Manager* manager, const string& name) {
    Error error;
    manager->PopProfile(name, &error);
    return error.type();
  }

  Error::Type TestPushProfile(Manager* manager, const string& name) {
    Error error;
    RpcIdentifier path;
    manager->PushProfile(name, &path, &error);
    return error.type();
  }

  Error::Type TestInsertUserProfile(Manager* manager,
                                    const string& name,
                                    const string& user_hash) {
    Error error;
    RpcIdentifier path;
    manager->InsertUserProfile(name, user_hash, &path, &error);
    return error.type();
  }

  scoped_refptr<MockProfile> AddNamedMockProfileToManager(
      Manager* manager, const RpcIdentifier& name) {
    scoped_refptr<MockProfile> profile(new MockProfile(manager, ""));
    EXPECT_CALL(*profile, GetRpcIdentifier()).WillRepeatedly(Return(name));
    EXPECT_CALL(*profile, UpdateDevice(_)).WillRepeatedly(Return(false));
    AdoptProfile(manager, profile);
    return profile;
  }

  void AddMockProfileToManager(Manager* manager) {
    AddNamedMockProfileToManager(manager, RpcIdentifier("/"));
  }

  void CompleteServiceSort() {
    EXPECT_TRUE(IsSortServicesTaskPending());
    dispatcher()->DispatchPendingEvents();
    EXPECT_FALSE(IsSortServicesTaskPending());
  }

  bool IsSortServicesTaskPending() {
    return !manager()->sort_services_task_.IsCancelled();
  }

  void RefreshConnectionState() { manager()->RefreshConnectionState(); }

  RpcIdentifier GetDefaultServiceRpcIdentifier() {
    return manager()->GetDefaultServiceRpcIdentifier(nullptr);
  }

  void SetResolver(Resolver* resolver) { manager()->resolver_ = resolver; }

  bool SetIgnoredDNSSearchPaths(const string& search_paths, Error* error) {
    return manager()->SetIgnoredDNSSearchPaths(search_paths, error);
  }

  bool SetCheckPortalList(const string& check_portal_list, Error* error) {
    return manager()->SetCheckPortalList(check_portal_list, error);
  }

  bool SetPortalFallbackUrlsString(const string& urls, Error* error) {
    return manager()->SetPortalFallbackUrlsString(urls, error);
  }

  const string& GetIgnoredDNSSearchPaths() {
    return manager()->props_.ignored_dns_search_paths;
  }

  const vector<string>& GetPortalFallbackUrlsString() {
    return manager()->props_.portal_fallback_http_urls;
  }

  size_t GetDefaultServiceObserverCount() const {
    size_t count = 0;
    for (auto& observer : manager()->default_service_observers_) {
      (void)observer;
      ++count;
    }
    return count;
  }

#if !defined(DISABLE_WIFI)
  WiFiServiceRefPtr ReleaseTempMockService() {
    // Take a reference to hold during this function.
    WiFiServiceRefPtr temp_service = temp_mock_service_;
    temp_mock_service_ = nullptr;
    return temp_service;
  }
#endif  // DISABLE_WIFI

  void VerifyPassiveMode() {
    EXPECT_NE(nullptr, manager()->device_claimer_);
    EXPECT_TRUE(manager()->device_claimer_->default_claimer());
  }

 protected:
  using MockServiceRefPtr = scoped_refptr<MockService>;

  class ServiceWatcher : public DefaultServiceObserver {
   public:
    MOCK_METHOD(void,
                OnDefaultServiceChanged,
                (const ServiceRefPtr& logical_service,
                 bool logical_service_changed,
                 const ServiceRefPtr& physical_service,
                 bool physical_service_changed));
  };

  class TerminationActionTest
      : public base::SupportsWeakPtr<TerminationActionTest> {
   public:
    static const char kActionName[];

    TerminationActionTest() : manager_(nullptr) {}
    virtual ~TerminationActionTest() = default;

    MOCK_METHOD(void, Done, (const Error&));

    void Action() { manager_->TerminationActionComplete("action"); }

    void set_manager(Manager* manager) { manager_ = manager; }

   private:
    Manager* manager_;
    DISALLOW_COPY_AND_ASSIGN(TerminationActionTest);
  };

  class DestinationVerificationTest
      : public base::SupportsWeakPtr<DestinationVerificationTest> {
   public:
    DestinationVerificationTest() = default;
    virtual ~DestinationVerificationTest() = default;

    MOCK_METHOD(void, ResultBoolCallbackStub, (const Error&, bool));
    MOCK_METHOD(void, ResultStringCallbackStub, (const Error&, const string&));

   private:
    DISALLOW_COPY_AND_ASSIGN(DestinationVerificationTest);
  };

  class DisableTechnologyReplyHandler
      : public base::SupportsWeakPtr<DisableTechnologyReplyHandler> {
   public:
    DisableTechnologyReplyHandler() = default;
    virtual ~DisableTechnologyReplyHandler() = default;

    MOCK_METHOD(void, ReportResult, (const Error&));

   private:
    DISALLOW_COPY_AND_ASSIGN(DisableTechnologyReplyHandler);
  };

  class ResultCallbackObserver {
   public:
    ResultCallbackObserver()
        : result_callback_(Bind(&ResultCallbackObserver::OnResultCallback,
                                Unretained(this))) {}
    virtual ~ResultCallbackObserver() = default;

    MOCK_METHOD(void, OnResultCallback, (const Error&));

    const ResultCallback& result_callback() const { return result_callback_; }

   private:
    ResultCallback result_callback_;

    DISALLOW_COPY_AND_ASSIGN(ResultCallbackObserver);
  };

  void SetSuspending(bool suspending) {
    power_manager_->suspending_ = suspending;
  }

  void SetPowerManager() {
    manager()->set_power_manager(power_manager_.release());
  }

  HookTable* GetTerminationActions() {
    return &manager()->termination_actions_;
  }

  void OnSuspendImminent() { manager()->OnSuspendImminent(); }

  void OnDarkSuspendImminent() { manager()->OnDarkSuspendImminent(); }

  void OnSuspendDone() { manager()->OnSuspendDone(); }

  void OnSuspendActionsComplete(const Error& error) {
    manager()->OnSuspendActionsComplete(error);
  }

  vector<RpcIdentifier> EnumerateAvailableServices() {
    return manager()->EnumerateAvailableServices(nullptr);
  }

  vector<RpcIdentifier> EnumerateWatchedServices() {
    return manager()->EnumerateWatchedServices(nullptr);
  }

  MockServiceRefPtr MakeAutoConnectableService() {
    MockServiceRefPtr service = new NiceMock<MockService>(manager());
    service->SetAutoConnect(true);
    service->SetConnectable(true);
    return service;
  }

#if !defined(DISABLE_WIRED_8021X)
  void SetEapProviderService(const ServiceRefPtr& service) {
    ethernet_eap_provider_->set_service(service);
  }
#endif  // DISABLE_WIRED_8021X

  const std::vector<Technology>& GetTechnologyOrder() {
    return manager()->technology_order_;
  }

  std::unique_ptr<MockPowerManager> power_manager_;
  vector<scoped_refptr<MockDevice>> mock_devices_;
  std::unique_ptr<MockDeviceInfo> device_info_;

#if !defined(DISABLE_WIFI)
  // This service is held for the manager, and given ownership in a mock
  // function.  This ensures that when the Manager takes ownership, there
  // is only one reference left.
  scoped_refptr<MockWiFiService> temp_mock_service_;
#endif  // DISABLE_WIFI

  // These pointers are owned by the manager, and only tracked here for
  // EXPECT*()
  ManagerMockAdaptor* manager_adaptor_;
  MockEthernetProvider* ethernet_provider_;
#if !defined(DISABLE_WIRED_8021X)
  MockEthernetEapProvider* ethernet_eap_provider_;
#endif  // DISABLE_WIRED_8021X
#if !defined(DISABLE_WIFI)
  MockWiFiProvider* wifi_provider_;
#endif  // DISABLE_WIFI
  MockThrottler* throttler_;
  MockUpstart* upstart_;
};

const char ManagerTest::TerminationActionTest::kActionName[] = "action";

bool ManagerTest::ServiceOrderIs(ServiceRefPtr svc0, ServiceRefPtr svc1) {
  if (!manager()->sort_services_task_.IsCancelled()) {
    manager()->SortServicesTask();
  }
  return (svc0.get() == manager()->services_[0].get() &&
          svc1.get() == manager()->services_[1].get());
}

void SetErrorPermissionDenied(Error* error) {
  error->Populate(Error::kPermissionDenied);
}

void SetErrorSuccess(Error* error) {
  error->Reset();
}

TEST_F(ManagerTest, Contains) {
  EXPECT_TRUE(manager()->store().Contains(kStateProperty));
  EXPECT_FALSE(manager()->store().Contains(""));
}

TEST_F(ManagerTest, PassiveModeDeviceRegistration) {
  manager()->SetPassiveMode();
  VerifyPassiveMode();

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));

  // Device not released, should not be registered.
  manager()->RegisterDevice(mock_devices_[0]);
  EXPECT_FALSE(IsDeviceRegistered(mock_devices_[0], Technology::kEthernet));

  // Device is released, should be registered.
  bool claimer_removed;
  Error error;
  manager()->ReleaseDevice("", mock_devices_[0]->link_name(), &claimer_removed,
                           &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(claimer_removed);
  manager()->RegisterDevice(mock_devices_[0]);
  EXPECT_TRUE(IsDeviceRegistered(mock_devices_[0], Technology::kEthernet));
}

TEST_F(ManagerTest, DeviceRegistration) {
  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  EXPECT_TRUE(IsDeviceRegistered(mock_devices_[0], Technology::kEthernet));
  EXPECT_TRUE(IsDeviceRegistered(mock_devices_[1], Technology::kWifi));
  EXPECT_TRUE(IsDeviceRegistered(mock_devices_[2], Technology::kCellular));
}

TEST_F(ManagerTest, DeviceRegistrationTriggersThrottler) {
  manager()->network_throttling_enabled_ = true;
  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));

  EXPECT_CALL(*throttler_, ThrottleInterfaces(_, _, _)).Times(1);
  EXPECT_CALL(*throttler_, ApplyThrottleToNewInterface(_)).Times(2);

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);
}

TEST_F(ManagerTest, ManagerCallsThrottlerCorrectly) {
  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  int ulrate = 1024;
  int dlrate = 2048;
  ResultCallback dummy;

  EXPECT_CALL(*throttler_, ThrottleInterfaces(_, ulrate, dlrate));
  manager()->SetNetworkThrottlingStatus(dummy, true, ulrate, dlrate);
  EXPECT_CALL(*throttler_, DisableThrottlingOnAllInterfaces(_));
  manager()->SetNetworkThrottlingStatus(dummy, false, ulrate, dlrate);
}

TEST_F(ManagerTest, DeviceRegistrationAndStart) {
  manager()->running_ = true;
  mock_devices_[0]->enabled_persistent_ = true;
  mock_devices_[1]->enabled_persistent_ = false;
  EXPECT_CALL(*mock_devices_[0], SetEnabled(true)).Times(1);
  EXPECT_CALL(*mock_devices_[1], SetEnabled(_)).Times(0);
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
}

TEST_F(ManagerTest, DeviceRegistrationWithProfile) {
  MockProfile* profile = new MockProfile(manager(), "");
  DeviceRefPtr device_ref(mock_devices_[0].get());
  AdoptProfile(manager(), profile);  // Passes ownership.
  EXPECT_CALL(*profile, ConfigureDevice(device_ref));
  EXPECT_CALL(*profile, UpdateDevice(device_ref));
  manager()->RegisterDevice(mock_devices_[0]);
}

TEST_F(ManagerTest, DeviceDeregistration) {
  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);

  ASSERT_TRUE(IsDeviceRegistered(mock_devices_[0], Technology::kEthernet));
  ASSERT_TRUE(IsDeviceRegistered(mock_devices_[1], Technology::kWifi));

  MockProfile* profile = new MockProfile(manager(), "");
  AdoptProfile(manager(), profile);  // Passes ownership.

  EXPECT_CALL(*mock_devices_[0], SetEnabled(false));
  EXPECT_CALL(*profile, UpdateDevice(DeviceRefPtr(mock_devices_[0])));
  manager()->DeregisterDevice(mock_devices_[0]);
  EXPECT_FALSE(IsDeviceRegistered(mock_devices_[0], Technology::kEthernet));

  EXPECT_CALL(*mock_devices_[1], SetEnabled(false));
  EXPECT_CALL(*profile, UpdateDevice(DeviceRefPtr(mock_devices_[1])));
  manager()->DeregisterDevice(mock_devices_[1]);
  EXPECT_FALSE(IsDeviceRegistered(mock_devices_[1], Technology::kWifi));
}

TEST_F(ManagerTest, ServiceRegistration) {
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), string());
  ProfileRefPtr profile(CreateProfileForManager(&manager));
  ASSERT_NE(nullptr, profile);
  AdoptProfile(&manager, profile);

  MockServiceRefPtr mock_service(new NiceMock<MockService>(&manager));
  MockServiceRefPtr mock_service2(new NiceMock<MockService>(&manager));

  RpcIdentifier service1_name(mock_service->unique_name());
  RpcIdentifier service2_name(mock_service2->unique_name());

  EXPECT_CALL(*mock_service, GetRpcIdentifier())
      .WillRepeatedly(Return(service1_name));
  EXPECT_CALL(*mock_service2, GetRpcIdentifier())
      .WillRepeatedly(Return(service2_name));
  // TODO(quiche): make this EXPECT_CALL work (crbug.com/203247)
  // EXPECT_CALL(*static_cast<ManagerMockAdaptor*>(manager.adaptor_.get()),
  //             EmitRpcIdentifierArrayChanged(kServicesProperty, _));

  manager.RegisterService(mock_service);
  manager.RegisterService(mock_service2);

  Error error;
  vector<RpcIdentifier> rpc_ids = manager.EnumerateAvailableServices(&error);
  set<RpcIdentifier> ids(rpc_ids.begin(), rpc_ids.end());
  EXPECT_EQ(2, ids.size());
  EXPECT_TRUE(base::ContainsKey(ids, mock_service->GetRpcIdentifier()));
  EXPECT_TRUE(base::ContainsKey(ids, mock_service2->GetRpcIdentifier()));

  EXPECT_NE(nullptr, manager.FindService(service1_name));
  EXPECT_NE(nullptr, manager.FindService(service2_name));

  manager.set_power_manager(power_manager_.release());
  manager.Stop();
}

TEST_F(ManagerTest, RegisterKnownService) {
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), string());
  ProfileRefPtr profile(CreateProfileForManager(&manager));
  ASSERT_NE(nullptr, profile);
  AdoptProfile(&manager, profile);
  {
    ServiceRefPtr service1(new ServiceUnderTest(&manager));
    ASSERT_TRUE(profile->AdoptService(service1));
    ASSERT_TRUE(profile->ContainsService(service1));
  }  // Force destruction of service1.

  ServiceRefPtr service2(new ServiceUnderTest(&manager));
  manager.RegisterService(service2);
  EXPECT_EQ(service2->profile(), profile);

  manager.set_power_manager(power_manager_.release());
  manager.Stop();
}

TEST_F(ManagerTest, RegisterUnknownService) {
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), string());
  ProfileRefPtr profile(CreateProfileForManager(&manager));
  ASSERT_NE(nullptr, profile);
  AdoptProfile(&manager, profile);
  {
    ServiceRefPtr service1(new ServiceUnderTest(&manager));
    ASSERT_TRUE(profile->AdoptService(service1));
    ASSERT_TRUE(profile->ContainsService(service1));
  }  // Force destruction of service1.
  MockServiceRefPtr mock_service2(new NiceMock<MockService>(&manager));
  EXPECT_CALL(*mock_service2, GetStorageIdentifier())
      .WillRepeatedly(Return(mock_service2->unique_name()));
  manager.RegisterService(mock_service2);
  EXPECT_NE(mock_service2->profile(), profile);

  manager.set_power_manager(power_manager_.release());
  manager.Stop();
}

TEST_F(ManagerTest, DeregisterUnregisteredService) {
  // WiFi assumes that it can deregister a service that is not
  // registered.  (E.g. a hidden service can be deregistered when it
  // loses its last endpoint, and again when WiFi is Stop()-ed.)
  //
  // So test that doing so doesn't cause a crash.
  MockServiceRefPtr service = new NiceMock<MockService>(manager());
  manager()->DeregisterService(service);
}

TEST_F(ManagerTest, GetProperties) {
  AddMockProfileToManager(manager());
  {
    brillo::VariantDictionary props;
    Error error;
    string expected("portal_list");
    manager()->mutable_store()->SetStringProperty(kCheckPortalListProperty,
                                                  expected, &error);
    manager()->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kCheckPortalListProperty) == props.end());
    EXPECT_TRUE(props[kCheckPortalListProperty].IsTypeCompatible<string>());
    EXPECT_EQ(props[kCheckPortalListProperty].Get<string>(), expected);
  }
  {
    brillo::VariantDictionary props;
    Error error;
    bool expected = true;
    manager()->mutable_store()->SetBoolProperty(kOfflineModeProperty, expected,
                                                &error);
    manager()->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kOfflineModeProperty) == props.end());
    EXPECT_TRUE(props[kOfflineModeProperty].IsTypeCompatible<bool>());
    EXPECT_EQ(props[kOfflineModeProperty].Get<bool>(), expected);
  }
}

TEST_F(ManagerTest, GetDevicesProperty) {
  AddMockProfileToManager(manager());
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  {
    brillo::VariantDictionary props;
    Error error;
    manager()->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kDevicesProperty) == props.end());
    EXPECT_TRUE(
        props[kDevicesProperty].IsTypeCompatible<vector<dbus::ObjectPath>>());
    vector<dbus::ObjectPath> devices =
        props[kDevicesProperty].Get<vector<dbus::ObjectPath>>();
    EXPECT_EQ(2, devices.size());
  }
}

TEST_F(ManagerTest, GetServicesProperty) {
  AddMockProfileToManager(manager());
  brillo::VariantDictionary props;
  Error error;
  manager()->store().GetProperties(&props, &error);
  ASSERT_FALSE(props.find(kServicesProperty) == props.end());
  EXPECT_TRUE(
      props[kServicesProperty].IsTypeCompatible<vector<dbus::ObjectPath>>());
}

TEST_F(ManagerTest, MoveService) {
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), string());
  MockServiceRefPtr s2(new MockService(&manager));
  // Inject an actual profile, backed by a fake StoreInterface
  {
    Profile::Identifier id("irrelevant");
    ProfileRefPtr profile(new Profile(&manager, id, FilePath(), false));
    auto storage = std::make_unique<MockStore>();
    EXPECT_CALL(*storage, ContainsGroup(s2->GetStorageIdentifier()))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*storage, Flush())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    profile->SetStorageForTest(std::move(storage));
    AdoptProfile(&manager, profile);
  }
  // Create a profile that already has |s2| in it.
  ProfileRefPtr profile(new EphemeralProfile(&manager));
  EXPECT_TRUE(profile->AdoptService(s2));

  // Now, move the Service |s2| to another profile.
  EXPECT_CALL(*s2, Save(_)).WillOnce(Return(true));
  ASSERT_TRUE(manager.MoveServiceToProfile(s2, manager.ActiveProfile()));

  // Force destruction of the original Profile, to ensure that the Service
  // is kept alive and populated with data.
  profile = nullptr;
  ASSERT_TRUE(manager.ActiveProfile()->ContainsService(s2));
  manager.set_power_manager(power_manager_.release());
  manager.Stop();
}

TEST_F(ManagerTest, LookupProfileByRpcIdentifier) {
  scoped_refptr<MockProfile> mock_profile(new MockProfile(manager(), ""));
  const RpcIdentifier kProfileName("profile0");
  EXPECT_CALL(*mock_profile, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName));
  AdoptProfile(manager(), mock_profile);

  EXPECT_FALSE(manager()->LookupProfileByRpcIdentifier(RpcIdentifier("foo")));
  ProfileRefPtr profile = manager()->LookupProfileByRpcIdentifier(kProfileName);
  EXPECT_EQ(mock_profile, profile);
}

TEST_F(ManagerTest, SetProfileForService) {
  scoped_refptr<MockProfile> profile0(new MockProfile(manager(), ""));
  RpcIdentifier profile_name0("profile0");
  EXPECT_CALL(*profile0, GetRpcIdentifier())
      .WillRepeatedly(Return(profile_name0));
  AdoptProfile(manager(), profile0);
  MockServiceRefPtr service(new MockService(manager()));
  EXPECT_FALSE(manager()->HasService(service));
  {
    Error error;
    EXPECT_CALL(*profile0, AdoptService(_)).WillOnce(Return(true));
    // Expect that setting the profile of a service that does not already
    // have one assigned does not cause a crash.
    manager()->SetProfileForService(service, RpcIdentifier("profile0"), &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  // The service should be registered as a side-effect of the profile being
  // set for this service.
  EXPECT_TRUE(manager()->HasService(service));

  // Since we have mocked Profile::AdoptServie() above, the service's
  // profile was not actually changed.  Do so explicitly now.
  service->set_profile(profile0);

  {
    Error error;
    manager()->SetProfileForService(service, RpcIdentifier("foo"), &error);
    EXPECT_EQ(Error::kInvalidArguments, error.type());
    EXPECT_EQ("Unknown Profile foo requested for Service", error.message());
  }

  {
    Error error;
    manager()->SetProfileForService(service, profile_name0, &error);
    EXPECT_EQ(Error::kInvalidArguments, error.type());
    EXPECT_EQ("Service is already connected to this profile", error.message());
  }

  scoped_refptr<MockProfile> profile1(new MockProfile(manager(), ""));
  RpcIdentifier profile_name1("profile1");
  EXPECT_CALL(*profile1, GetRpcIdentifier())
      .WillRepeatedly(Return(profile_name1));
  AdoptProfile(manager(), profile1);

  {
    Error error;
    EXPECT_CALL(*profile1, AdoptService(_)).WillOnce(Return(true));
    EXPECT_CALL(*profile0, AbandonService(_)).WillOnce(Return(true));
    manager()->SetProfileForService(service, profile_name1, &error);
    EXPECT_TRUE(error.IsSuccess());
  }
}

TEST_F(ManagerTest, CreateProfile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  // Invalid name should be rejected.
  EXPECT_EQ(Error::kInvalidArguments, TestCreateProfile(&manager, ""));

  // A profile with invalid characters in it should similarly be rejected.
  EXPECT_EQ(Error::kInvalidArguments,
            TestCreateProfile(&manager, "valid_profile"));

  // We should be able to create a machine profile.
  EXPECT_EQ(Error::kSuccess, TestCreateProfile(&manager, "valid"));

  // We should succeed in creating a valid user profile.  Verify the returned
  // path.
  const char kProfile[] = "~user/profile";
  {
    Error error;
    RpcIdentifier path;
    ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
    manager.CreateProfile(kProfile, &path, &error);
    EXPECT_EQ(Error::kSuccess, error.type());
    EXPECT_EQ(RpcIdentifier("/profile_rpc"), path);
  }

  // We should fail in creating it a second time (already exists).
  EXPECT_EQ(Error::kAlreadyExists, TestCreateProfile(&manager, kProfile));
}

TEST_F(ManagerTest, PushPopProfile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());
  vector<ProfileRefPtr>& profiles = GetProfiles(&manager);

  // Pushing an invalid profile should fail.
  EXPECT_EQ(Error::kInvalidArguments, TestPushProfile(&manager, ""));

  // Create and push a default profile. Should succeed.
  const char kDefaultProfile0[] = "default";
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kDefaultProfile0));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kDefaultProfile0));
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kDefaultProfile0));

  // Pushing a default profile that does not exist on disk will _not_
  // fail, because we'll use temporary storage for it.
  const char kMissingDefaultProfile[] = "missingdefault";
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kMissingDefaultProfile));
  EXPECT_EQ(1, profiles.size());
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kMissingDefaultProfile));
  EXPECT_EQ(0, profiles.size());

  const char kProfile0[] = "~user/profile0";
  const char kProfile1[] = "~user/profile1";
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));

  // Create a couple of profiles.
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kProfile0));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kProfile1));

  // Push these profiles on the stack.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile0));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile1));

  // Pushing a profile a second time should fail.
  EXPECT_EQ(Error::kAlreadyExists, TestPushProfile(&manager, kProfile0));
  EXPECT_EQ(Error::kAlreadyExists, TestPushProfile(&manager, kProfile1));

  Error error;
  // Active profile should be the last one we pushed.
  EXPECT_EQ(kProfile1, "~" + manager.ActiveProfile()->GetFriendlyName());

  // Make sure a profile name that doesn't exist fails.
  const char kProfile2Id[] = "profile2";
  const string kProfile2 = base::StringPrintf("~user/%s", kProfile2Id);
  EXPECT_EQ(Error::kNotFound, TestPushProfile(&manager, kProfile2));

  // Create a new service, with a specific storage name.
  MockServiceRefPtr service(new NiceMock<MockService>(&manager));
  const char kServiceName[] = "service_storage_name";
  EXPECT_CALL(*service, GetStorageIdentifier())
      .WillRepeatedly(Return(kServiceName));
  EXPECT_CALL(*service, Load(_)).WillRepeatedly(Return(true));

  // Add this service to the manager -- it should end up in the ephemeral
  // profile.
  manager.RegisterService(service);
  ASSERT_EQ(GetEphemeralProfile(&manager), service->profile());

  // Create storage for a profile that contains the service storage name.
  ASSERT_TRUE(CreateBackingStoreForService(&temp_dir, "user", kProfile2Id,
                                           kServiceName));

  // When we push the profile, the service should move away from the
  // ephemeral profile to this new profile since it has an entry for
  // this service.
  EXPECT_CALL(*service, ClearExplicitlyDisconnected());
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile2));
  EXPECT_NE(GetEphemeralProfile(&manager), service->profile());
  EXPECT_EQ(kProfile2, "~" + service->profile()->GetFriendlyName());

  // Insert another profile that should supersede ownership of the service.
  const char kProfile3Id[] = "profile3";
  const string kProfile3 = base::StringPrintf("~user/%s", kProfile3Id);
  ASSERT_TRUE(CreateBackingStoreForService(&temp_dir, "user", kProfile3Id,
                                           kServiceName));
  // We don't verify this expectation inline, since this would clear other
  // recurring expectations on the service.
  EXPECT_CALL(*service, ClearExplicitlyDisconnected());
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile3));
  EXPECT_EQ(kProfile3, "~" + service->profile()->GetFriendlyName());

  // Popping an invalid profile name should fail.
  EXPECT_EQ(Error::kInvalidArguments, TestPopProfile(&manager, "~"));

  // Popping an profile that is not at the top of the stack should fail.
  EXPECT_EQ(Error::kNotSupported, TestPopProfile(&manager, kProfile0));

  // Popping the top profile should succeed.
  EXPECT_CALL(*service, ClearExplicitlyDisconnected());
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kProfile3));

  // Moreover the service should have switched profiles to profile 2.
  EXPECT_EQ(kProfile2, "~" + service->profile()->GetFriendlyName());

  // Popping the top profile should succeed.
  EXPECT_CALL(*service, ClearExplicitlyDisconnected());
  EXPECT_EQ(Error::kSuccess, TestPopAnyProfile(&manager));

  // The service should now revert to the ephemeral profile.
  EXPECT_EQ(GetEphemeralProfile(&manager), service->profile());

  // Pop the remaining two profiles off the stack.
  EXPECT_CALL(*service, ClearExplicitlyDisconnected()).Times(2);
  EXPECT_EQ(Error::kSuccess, TestPopAnyProfile(&manager));
  EXPECT_EQ(Error::kSuccess, TestPopAnyProfile(&manager));
  Mock::VerifyAndClearExpectations(service.get());

  // Next pop should fail with "stack is empty".
  EXPECT_EQ(Error::kNotFound, TestPopAnyProfile(&manager));

  const char kMachineProfile0[] = "machineprofile0";
  const char kMachineProfile1[] = "machineprofile1";
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kMachineProfile0));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kMachineProfile1));

  // Should be able to push a machine profile.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kMachineProfile0));

  // Should be able to push a user profile atop a machine profile.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile0));

  // Pushing a system-wide profile on top of a user profile should fail.
  EXPECT_EQ(Error::kInvalidArguments,
            TestPushProfile(&manager, kMachineProfile1));

  // However if we pop the user profile, we should be able stack another
  // machine profile on.
  EXPECT_EQ(Error::kSuccess, TestPopAnyProfile(&manager));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kMachineProfile1));

  // Add two user profiles to the top of the stack.
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile0));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile1));
  EXPECT_EQ(4, profiles.size());

  // PopAllUserProfiles should remove both user profiles, leaving the two
  // machine profiles.
  EXPECT_EQ(Error::kSuccess, TestPopAllUserProfiles(&manager));
  EXPECT_EQ(2, profiles.size());
  EXPECT_TRUE(profiles[0]->GetUser().empty());
  EXPECT_TRUE(profiles[1]->GetUser().empty());

  EXPECT_TRUE(manager.IsTechnologyAutoConnectDisabled(Technology::kCellular));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kEthernet));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kWifi));

  // Use InsertUserProfile() instead.  Although a machine profile is valid
  // in this state, it cannot be added via InsertUserProfile.
  EXPECT_EQ(Error::kSuccess, TestPopProfile(&manager, kMachineProfile1));
  EXPECT_EQ(Error::kInvalidArguments,
            TestInsertUserProfile(&manager, kMachineProfile1, "machinehash1"));
  const char kUserHash0[] = "userhash0";
  const char kUserHash1[] = "userhash1";
  EXPECT_EQ(Error::kSuccess,
            TestInsertUserProfile(&manager, kProfile0, kUserHash0));

  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kCellular));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kEthernet));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kWifi));

  EXPECT_EQ(Error::kSuccess,
            TestInsertUserProfile(&manager, kProfile1, kUserHash1));

  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kCellular));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kEthernet));
  EXPECT_FALSE(manager.IsTechnologyAutoConnectDisabled(Technology::kWifi));

  EXPECT_EQ(3, profiles.size());
  EXPECT_EQ(kUserHash0, profiles[1]->GetUserHash());
  EXPECT_EQ(kUserHash1, profiles[2]->GetUserHash());
}

TEST_F(ManagerTest, RemoveProfile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  const char kProfile0[] = "profile0";
  FilePath profile_path(Profile::GetFinalStoragePath(
      FilePath(storage_path()), Profile::Identifier(kProfile0)));

  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kProfile0));
  ASSERT_TRUE(base::PathExists(profile_path));

  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile0));

  // Remove should fail since the profile is still on the stack.
  {
    Error error;
    manager.RemoveProfile(kProfile0, &error);
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }

  // Profile path should still exist.
  EXPECT_TRUE(base::PathExists(profile_path));

  EXPECT_EQ(Error::kSuccess, TestPopAnyProfile(&manager));

  // This should succeed now that the profile is off the stack.
  {
    Error error;
    manager.RemoveProfile(kProfile0, &error);
    EXPECT_EQ(Error::kSuccess, error.type());
  }

  // Profile path should no longer exist.
  EXPECT_FALSE(base::PathExists(profile_path));

  // Another remove succeeds, due to a foible in base::DeleteFile --
  // it is not an error to delete a file that does not exist.
  {
    Error error;
    manager.RemoveProfile(kProfile0, &error);
    EXPECT_EQ(Error::kSuccess, error.type());
  }

  // Let's create an error case that will "work".  Create a non-empty
  // directory in the place of the profile pathname.
  ASSERT_TRUE(base::CreateDirectory(profile_path.Append("foo")));
  {
    Error error;
    manager.RemoveProfile(kProfile0, &error);
    EXPECT_EQ(Error::kOperationFailed, error.type());
  }
}

TEST_F(ManagerTest, RemoveService) {
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));

  // Used in expectations which cannot accept a mock refptr.
  const ServiceRefPtr& service = mock_service;

  manager()->RegisterService(service);
  EXPECT_EQ(GetEphemeralProfile(manager()), service->profile());

  scoped_refptr<MockProfile> profile(
      new StrictMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);

  // If service is ephemeral, it should be unloaded and left ephemeral.
  EXPECT_CALL(*profile, AbandonService(service)).Times(0);
  EXPECT_CALL(*profile, ConfigureService(service)).Times(0);
  EXPECT_CALL(*mock_service, Unload()).WillOnce(Return(false));
  manager()->RemoveService(service);
  Mock::VerifyAndClearExpectations(mock_service.get());
  Mock::VerifyAndClearExpectations(profile.get());
  EXPECT_EQ(GetEphemeralProfile(manager()), service->profile());
  EXPECT_TRUE(manager()->HasService(service));  // Since Unload() was false.

  // If service is not ephemeral and the Manager finds a profile to assign
  // the service to, the service should be re-parented.  Note that since we
  // are using a MockProfile, ConfigureService() never actually changes the
  // Service's profile.
  service->set_profile(profile);
  EXPECT_CALL(*profile, AbandonService(service));
  EXPECT_CALL(*profile, ConfigureService(service)).WillOnce(Return(true));
  EXPECT_CALL(*mock_service, Unload()).Times(0);
  manager()->RemoveService(service);
  Mock::VerifyAndClearExpectations(mock_service.get());
  Mock::VerifyAndClearExpectations(profile.get());
  EXPECT_TRUE(manager()->HasService(service));
  EXPECT_EQ(profile, service->profile());

  // If service becomes ephemeral since there is no profile to support it,
  // it should be unloaded.
  EXPECT_CALL(*profile, AbandonService(service));
  EXPECT_CALL(*profile, ConfigureService(service)).WillOnce(Return(false));
  EXPECT_CALL(*mock_service, Unload()).WillOnce(Return(true));
  manager()->RemoveService(service);
  EXPECT_FALSE(manager()->HasService(service));
}

TEST_F(ManagerTest, CreateDuplicateProfileWithMissingKeyfile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());

  const char kProfile0[] = "profile0";
  FilePath profile_path(Profile::GetFinalStoragePath(
      FilePath(storage_path()), Profile::Identifier(kProfile0)));
  ASSERT_EQ(Error::kSuccess, TestCreateProfile(&manager, kProfile0));
  ASSERT_TRUE(base::PathExists(profile_path));
  EXPECT_EQ(Error::kSuccess, TestPushProfile(&manager, kProfile0));

  // Ensure that even if the backing filestore is removed, we still can't
  // create a profile twice.
  ASSERT_TRUE(base::DeleteFile(profile_path, false));
  EXPECT_EQ(Error::kAlreadyExists, TestCreateProfile(&manager, kProfile0));
}

TEST_F(ManagerTest, HandleProfileEntryDeletion) {
  MockServiceRefPtr s_not_in_profile(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_not_in_group(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_configure_fail(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_configure_succeed(new NiceMock<MockService>(manager()));

  string entry_name("entry_name");
  EXPECT_CALL(*ethernet_provider_, RefreshGenericEthernetService());
  EXPECT_CALL(*s_not_in_profile, GetStorageIdentifier()).Times(0);
  EXPECT_CALL(*s_not_in_group, GetStorageIdentifier())
      .WillRepeatedly(Return("not_entry_name"));
  EXPECT_CALL(*s_configure_fail, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));
  EXPECT_CALL(*s_configure_succeed, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));

  manager()->RegisterService(s_not_in_profile);
  manager()->RegisterService(s_not_in_group);
  manager()->RegisterService(s_configure_fail);
  manager()->RegisterService(s_configure_succeed);

  scoped_refptr<MockProfile> profile0(
      new StrictMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(
      new StrictMock<MockProfile>(manager(), ""));

  s_not_in_group->set_profile(profile1);
  s_configure_fail->set_profile(profile1);
  s_configure_succeed->set_profile(profile1);

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);

  CompleteServiceSort();

  // No services are a member of this profile.
  EXPECT_FALSE(manager()->HandleProfileEntryDeletion(profile0, entry_name));
  EXPECT_FALSE(IsSortServicesTaskPending());

  // No services that are members of this profile have this entry name.
  EXPECT_FALSE(manager()->HandleProfileEntryDeletion(profile1, ""));
  EXPECT_FALSE(IsSortServicesTaskPending());

  // Only services that are members of the profile and group will be abandoned.
  EXPECT_CALL(*profile1, AbandonService(IsRefPtrTo(s_not_in_profile.get())))
      .Times(0);
  EXPECT_CALL(*profile1, AbandonService(IsRefPtrTo(s_not_in_group.get())))
      .Times(0);
  EXPECT_CALL(*profile1, AbandonService(IsRefPtrTo(s_configure_fail.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile1, AbandonService(IsRefPtrTo(s_configure_succeed.get())))
      .WillOnce(Return(true));

  // Never allow services to re-join profile1.
  EXPECT_CALL(*profile1, ConfigureService(_)).WillRepeatedly(Return(false));

  // Only allow one of the members of the profile and group to successfully
  // join profile0.
  EXPECT_CALL(*profile0, ConfigureService(IsRefPtrTo(s_not_in_profile.get())))
      .Times(0);
  EXPECT_CALL(*profile0, ConfigureService(IsRefPtrTo(s_not_in_group.get())))
      .Times(0);
  EXPECT_CALL(*profile0, ConfigureService(IsRefPtrTo(s_configure_fail.get())))
      .WillOnce(Return(false));
  EXPECT_CALL(*profile0,
              ConfigureService(IsRefPtrTo(s_configure_succeed.get())))
      .WillOnce(Return(true));

  // Expect the failed-to-configure service to have Unload() called on it.
  EXPECT_CALL(*s_not_in_profile, Unload()).Times(0);
  EXPECT_CALL(*s_not_in_group, Unload()).Times(0);
  EXPECT_CALL(*s_configure_fail, Unload()).Times(1);
  EXPECT_CALL(*s_configure_succeed, Unload()).Times(0);

  EXPECT_TRUE(manager()->HandleProfileEntryDeletion(profile1, entry_name));
  EXPECT_TRUE(IsSortServicesTaskPending());

  EXPECT_EQ(GetEphemeralProfile(manager()), s_not_in_profile->profile());
  EXPECT_EQ(profile1, s_not_in_group->profile());
  EXPECT_EQ(GetEphemeralProfile(manager()), s_configure_fail->profile());

  // Since we are using a MockProfile, the profile does not actually change,
  // since ConfigureService was not actually called on the service.
  EXPECT_EQ(profile1, s_configure_succeed->profile());
}

TEST_F(ManagerTest, HandleProfileEntryDeletionWithUnload) {
  MockServiceRefPtr s_will_remove0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_remove1(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_not_remove0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_not_remove1(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr))
      .Times(4);  // Once for each registration.

  string entry_name("entry_name");
  EXPECT_CALL(*s_will_remove0, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));
  EXPECT_CALL(*s_will_remove1, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));
  EXPECT_CALL(*s_will_not_remove0, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));
  EXPECT_CALL(*s_will_not_remove1, GetStorageIdentifier())
      .WillRepeatedly(Return(entry_name));

  manager()->RegisterService(s_will_remove0);
  CompleteServiceSort();
  manager()->RegisterService(s_will_not_remove0);
  CompleteServiceSort();
  manager()->RegisterService(s_will_remove1);
  CompleteServiceSort();
  manager()->RegisterService(s_will_not_remove1);
  CompleteServiceSort();

  // One for each service added above.
  ASSERT_EQ(4, manager()->services_.size());

  scoped_refptr<MockProfile> profile(
      new StrictMock<MockProfile>(manager(), ""));

  s_will_remove0->set_profile(profile);
  s_will_remove1->set_profile(profile);
  s_will_not_remove0->set_profile(profile);
  s_will_not_remove1->set_profile(profile);

  AdoptProfile(manager(), profile);

  // Deny any of the services re-entry to the profile.
  EXPECT_CALL(*profile, ConfigureService(_)).WillRepeatedly(Return(false));

  EXPECT_CALL(*profile, AbandonService(ServiceRefPtr(s_will_remove0)))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile, AbandonService(ServiceRefPtr(s_will_remove1)))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile, AbandonService(ServiceRefPtr(s_will_not_remove0)))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile, AbandonService(ServiceRefPtr(s_will_not_remove1)))
      .WillOnce(Return(true));

  EXPECT_CALL(*s_will_remove0, Unload()).WillOnce(Return(true));
  EXPECT_CALL(*s_will_remove1, Unload()).WillOnce(Return(true));
  EXPECT_CALL(*s_will_not_remove0, Unload()).WillOnce(Return(false));
  EXPECT_CALL(*s_will_not_remove1, Unload()).WillOnce(Return(false));

  // This will cause all the profiles to be unloaded.
  EXPECT_FALSE(IsSortServicesTaskPending());
  EXPECT_TRUE(manager()->HandleProfileEntryDeletion(profile, entry_name));
  EXPECT_TRUE(IsSortServicesTaskPending());

  // 2 of the 4 services added above should have been unregistered and
  // removed, leaving 2.
  EXPECT_EQ(2, manager()->services_.size());
  EXPECT_EQ(s_will_not_remove0, manager()->services_[0]);
  EXPECT_EQ(s_will_not_remove1, manager()->services_[1]);
}

TEST_F(ManagerTest, PopProfileWithUnload) {
  MockServiceRefPtr s_will_remove0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_remove1(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_not_remove0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr s_will_not_remove1(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr))
      .Times(5);  // Once for each registration, and one after profile pop.

  manager()->RegisterService(s_will_remove0);
  CompleteServiceSort();
  manager()->RegisterService(s_will_not_remove0);
  CompleteServiceSort();
  manager()->RegisterService(s_will_remove1);
  CompleteServiceSort();
  manager()->RegisterService(s_will_not_remove1);
  CompleteServiceSort();

  // One for each service added above.
  ASSERT_EQ(4, manager()->services_.size());

  scoped_refptr<MockProfile> profile0(
      new StrictMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(
      new StrictMock<MockProfile>(manager(), ""));

  s_will_remove0->set_profile(profile1);
  s_will_remove1->set_profile(profile1);
  s_will_not_remove0->set_profile(profile1);
  s_will_not_remove1->set_profile(profile1);

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);

  // Deny any of the services entry to profile0, so they will all be unloaded.
  EXPECT_CALL(*profile0, ConfigureService(_)).WillRepeatedly(Return(false));

  EXPECT_CALL(*s_will_remove0, Unload()).WillOnce(Return(true));
  EXPECT_CALL(*s_will_remove1, Unload()).WillOnce(Return(true));
  EXPECT_CALL(*s_will_not_remove0, Unload()).WillRepeatedly(Return(false));
  EXPECT_CALL(*s_will_not_remove1, Unload()).WillOnce(Return(false));

  // Ignore calls to Profile::GetRpcIdentifier because of emitted changes of the
  // profile list.
  EXPECT_CALL(*profile0, GetRpcIdentifier()).Times(AnyNumber());
  EXPECT_CALL(*profile1, GetRpcIdentifier()).Times(AnyNumber());

  // This will pop profile1, which should cause all our profiles to unload.
  manager()->PopProfileInternal();
  CompleteServiceSort();

  // 2 of the 4 services added above should have been unregistered and
  // removed, leaving 2.
  EXPECT_EQ(2, manager()->services_.size());
  EXPECT_EQ(s_will_not_remove0, manager()->services_[0]);
  EXPECT_EQ(s_will_not_remove1, manager()->services_[1]);

  // Expect the unloaded services to lose their profile reference.
  EXPECT_FALSE(s_will_remove0->profile());
  EXPECT_FALSE(s_will_remove1->profile());

  // If we explicitly deregister a service, the effect should be the same
  // with respect to the profile reference.
  ASSERT_NE(nullptr, s_will_not_remove0->profile());
  manager()->DeregisterService(s_will_not_remove0);
  EXPECT_FALSE(s_will_not_remove0->profile());
}

TEST_F(ManagerTest, SetProperty) {
  {
    Error error;
    const bool offline_mode = true;
    EXPECT_TRUE(manager()->mutable_store()->SetAnyProperty(
        kOfflineModeProperty, brillo::Any(offline_mode), &error));
  }
  {
    Error error;
    const string portal_list("wifi,cellular");
    EXPECT_TRUE(manager()->mutable_store()->SetAnyProperty(
        kCheckPortalListProperty, brillo::Any(portal_list), &error));
  }
  // Attempt to write with value of wrong type should return InvalidArgs.
  {
    Error error;
    EXPECT_FALSE(manager()->mutable_store()->SetAnyProperty(
        kCheckPortalListProperty, PropertyStoreTest::kBoolV, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  {
    Error error;
    EXPECT_FALSE(manager()->mutable_store()->SetAnyProperty(
        kOfflineModeProperty, PropertyStoreTest::kStringV, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  // Attempt to write R/O property should return InvalidArgs.
  {
    Error error;
    EXPECT_FALSE(manager()->mutable_store()->SetAnyProperty(
        kEnabledTechnologiesProperty, PropertyStoreTest::kStringsV, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
}

TEST_F(ManagerTest, RequestScan) {
  {
    Error error;
    manager()->RegisterDevice(mock_devices_[0].get());
    manager()->RegisterDevice(mock_devices_[1].get());
    EXPECT_CALL(*mock_devices_[0], technology())
        .WillRepeatedly(Return(Technology::kWifi));
    EXPECT_CALL(*mock_devices_[0], Scan(_, _));
    EXPECT_CALL(*mock_devices_[1], technology())
        .WillRepeatedly(Return(Technology::kUnknown));
    EXPECT_CALL(*mock_devices_[1], Scan(_, _)).Times(0);
    EXPECT_CALL(*metrics(),
                NotifyUserInitiatedEvent(Metrics::kUserInitiatedEventWifiScan))
        .Times(1);
    manager()->RequestScan(kTypeWifi, &error);
    manager()->DeregisterDevice(mock_devices_[0].get());
    manager()->DeregisterDevice(mock_devices_[1].get());
    Mock::VerifyAndClearExpectations(mock_devices_[0].get());
    Mock::VerifyAndClearExpectations(mock_devices_[1].get());

    manager()->RegisterDevice(mock_devices_[0].get());
    EXPECT_CALL(*mock_devices_[0], technology())
        .WillRepeatedly(Return(Technology::kWifi));
    EXPECT_CALL(*metrics(),
                NotifyUserInitiatedEvent(Metrics::kUserInitiatedEventWifiScan))
        .Times(1);
    EXPECT_CALL(*mock_devices_[0], Scan(_, _));
    manager()->RequestScan(kTypeWifi, &error);
    manager()->DeregisterDevice(mock_devices_[0].get());
    Mock::VerifyAndClearExpectations(mock_devices_[0].get());

    manager()->RegisterDevice(mock_devices_[0].get());
    EXPECT_CALL(*mock_devices_[0], technology())
        .WillRepeatedly(Return(Technology::kUnknown));
    EXPECT_CALL(*metrics(),
                NotifyUserInitiatedEvent(Metrics::kUserInitiatedEventWifiScan))
        .Times(0);
    EXPECT_CALL(*mock_devices_[0], Scan(_, _)).Times(0);
    manager()->RequestScan(kTypeWifi, &error);
    manager()->DeregisterDevice(mock_devices_[0].get());
    Mock::VerifyAndClearExpectations(mock_devices_[0].get());
  }

  {
    Error error;
    manager()->RequestScan("bogus_device_type", &error);
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
}

TEST_F(ManagerTest, GetServiceNoType) {
  KeyValueStore args;
  Error e;
  manager()->GetService(args, &e);
  EXPECT_EQ(Error::kInvalidArguments, e.type());
  EXPECT_EQ("must specify service type", e.message());
}

TEST_F(ManagerTest, GetServiceUnknownType) {
  KeyValueStore args;
  Error e;
  args.SetString(kTypeProperty, kTypePPPoE);
  manager()->GetService(args, &e);
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_EQ("service type is unsupported", e.message());
}

TEST_F(ManagerTest, GetServiceEthernet) {
  KeyValueStore args;
  Error e;
  EthernetServiceRefPtr service;
  args.SetString(kTypeProperty, kTypeEthernet);
  EXPECT_CALL(*ethernet_provider_, GetService(_, _))
      .WillRepeatedly(Return(service));
  manager()->GetService(args, &e);
  EXPECT_TRUE(e.IsSuccess());
}

#if !defined(DISABLE_WIRED_8021X)
TEST_F(ManagerTest, GetServiceEthernetEap) {
  KeyValueStore args;
  Error e;
  ServiceRefPtr service = new NiceMock<MockService>(manager());
  args.SetString(kTypeProperty, kTypeEthernetEap);
  SetEapProviderService(service);
  EXPECT_EQ(service, manager()->GetService(args, &e));
  EXPECT_TRUE(e.IsSuccess());
}
#endif  // DISABLE_WIRED_8021X

#if !defined(DISABLE_WIFI)
TEST_F(ManagerTest, GetServiceWifi) {
  KeyValueStore args;
  Error e;
  WiFiServiceRefPtr wifi_service;
  args.SetString(kTypeProperty, kTypeWifi);
  EXPECT_CALL(*wifi_provider_, GetService(_, _))
      .WillRepeatedly(Return(wifi_service));
  manager()->GetService(args, &e);
  EXPECT_TRUE(e.IsSuccess());
}
#endif  // DISABLE_WIFI

TEST_F(ManagerTest, GetServiceVPNUnknownType) {
  KeyValueStore args;
  Error e;
  args.SetString(kTypeProperty, kTypeVPN);
  scoped_refptr<MockProfile> profile(
      new StrictMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);
  ServiceRefPtr service = manager()->GetService(args, &e);
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_FALSE(service);
}

TEST_F(ManagerTest, GetServiceVPN) {
  KeyValueStore args;
  Error e;
  args.SetString(kTypeProperty, kTypeVPN);
  args.SetString(kProviderTypeProperty, kProviderOpenVpn);
  args.SetString(kProviderHostProperty, "10.8.0.1");
  args.SetString(kNameProperty, "vpn-name");
  scoped_refptr<MockProfile> profile(
      new StrictMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);

#if defined(DISABLE_VPN)

  ServiceRefPtr service = manager()->GetService(args, &e);
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_FALSE(service);

#else

  ServiceRefPtr updated_service;
  EXPECT_CALL(*profile, UpdateService(_))
      .WillOnce(DoAll(SaveArg<0>(&updated_service), Return(true)));
  ServiceRefPtr configured_service;
  EXPECT_CALL(*profile, LoadService(_)).WillOnce(Return(false));
  EXPECT_CALL(*profile, ConfigureService(_))
      .WillOnce(DoAll(SaveArg<0>(&configured_service), Return(true)));
  ServiceRefPtr service = manager()->GetService(args, &e);
  EXPECT_TRUE(e.IsSuccess());
  EXPECT_NE(nullptr, service);
  EXPECT_EQ(service, updated_service);
  EXPECT_EQ(service, configured_service);

#endif  // DISABLE_VPN
}

TEST_F(ManagerTest, ConfigureServiceWithInvalidProfile) {
  // Manager calls ActiveProfile() so we need at least one profile installed.
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);

  KeyValueStore args;
  args.SetString(kProfileProperty, "xxx");
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("Invalid profile name xxx", error.message());
}

TEST_F(ManagerTest, ConfigureServiceWithGetServiceFailure) {
  // Manager calls ActiveProfile() so we need at least one profile installed.
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);

  KeyValueStore args;
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("must specify service type", error.message());
}

#if !defined(DISABLE_WIFI)
// TODO(zqiu): Consider creating a TestProvider to provide generic services,
// (MockService) instead of using technology specific (wifi) services. This
// will remove the dependency for wifi from ConfigureXXX tests.
//
// A registered service in the ephemeral profile should be moved to the
// active profile as a part of configuration if no profile was explicitly
// specified.
TEST_F(ManagerTest, ConfigureRegisteredServiceWithoutProfile) {
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(manager(), ""));

  AdoptProfile(manager(), profile);  // This is now the active profile.

  const vector<uint8_t> ssid;
  scoped_refptr<MockWiFiService> service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, ssid, "", "", false));

  manager()->RegisterService(service);
  service->set_profile(GetEphemeralProfile(manager()));

  EXPECT_CALL(*wifi_provider_, GetService(_, _)).WillOnce(Return(service));
  EXPECT_CALL(*profile, UpdateService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile, AdoptService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}

// If we configure a service that was already registered and explicitly
// specify a profile, it should be moved from the profile it was previously
// in to the specified profile if one was requested.
TEST_F(ManagerTest, ConfigureRegisteredServiceWithProfile) {
  scoped_refptr<MockProfile> profile0(new NiceMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(new NiceMock<MockProfile>(manager(), ""));

  const RpcIdentifier kProfileName0("profile0");
  const RpcIdentifier kProfileName1("profile1");

  EXPECT_CALL(*profile0, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName0));
  EXPECT_CALL(*profile1, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName1));

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);  // profile1 is now the ActiveProfile.

  const vector<uint8_t> ssid;
  scoped_refptr<MockWiFiService> service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, ssid, "", "", false));

  manager()->RegisterService(service);
  service->set_profile(profile1);

  EXPECT_CALL(*wifi_provider_, GetService(_, _)).WillOnce(Return(service));
  EXPECT_CALL(*profile0, LoadService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile0, UpdateService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile0, AdoptService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile1, AbandonService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kProfileProperty, kProfileName0);
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  service->set_profile(nullptr);  // Breaks refcounting loop.
}

// If we configure a service that is already a member of the specified
// profile, the Manager should not call LoadService or AdoptService again
// on this service.
TEST_F(ManagerTest, ConfigureRegisteredServiceWithSameProfile) {
  scoped_refptr<MockProfile> profile0(new NiceMock<MockProfile>(manager(), ""));

  const RpcIdentifier kProfileName0("profile0");

  EXPECT_CALL(*profile0, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName0));

  AdoptProfile(manager(), profile0);  // profile0 is now the ActiveProfile.

  const vector<uint8_t> ssid;
  scoped_refptr<MockWiFiService> service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, ssid, "", "", false));

  manager()->RegisterService(service);
  service->set_profile(profile0);

  EXPECT_CALL(*wifi_provider_, GetService(_, _)).WillOnce(Return(service));
  EXPECT_CALL(*profile0, LoadService(ServiceRefPtr(service.get()))).Times(0);
  EXPECT_CALL(*profile0, UpdateService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile0, AdoptService(ServiceRefPtr(service.get()))).Times(0);

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kProfileProperty, kProfileName0);
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_TRUE(error.IsSuccess());
  service->set_profile(nullptr);  // Breaks refcounting loop.
}

// An unregistered service should remain unregistered, but its contents should
// be saved to the specified profile nonetheless.
TEST_F(ManagerTest, ConfigureUnregisteredServiceWithProfile) {
  scoped_refptr<MockProfile> profile0(new NiceMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(new NiceMock<MockProfile>(manager(), ""));

  const RpcIdentifier kProfileName0("profile0");
  const RpcIdentifier kProfileName1("profile1");

  EXPECT_CALL(*profile0, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName0));
  EXPECT_CALL(*profile1, GetRpcIdentifier())
      .WillRepeatedly(Return(kProfileName1));

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);  // profile1 is now the ActiveProfile.

  const vector<uint8_t> ssid;
  scoped_refptr<MockWiFiService> service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, ssid, "", "", false));

  service->set_profile(profile1);

  EXPECT_CALL(*wifi_provider_, GetService(_, _)).WillOnce(Return(service));
  EXPECT_CALL(*profile0, UpdateService(ServiceRefPtr(service.get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*profile0, AdoptService(_)).Times(0);
  EXPECT_CALL(*profile1, AdoptService(_)).Times(0);

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kProfileProperty, kProfileName0);
  Error error;
  manager()->ConfigureService(args, &error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ManagerTest, ConfigureServiceForProfileWithNoType) {
  KeyValueStore args;
  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(RpcIdentifier(""), args, &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("must specify service type", error.message());
  EXPECT_EQ(nullptr, service);
}

TEST_F(ManagerTest, ConfigureServiceForProfileWithWrongType) {
  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeCellular);
  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(RpcIdentifier(""), args, &error);
  EXPECT_EQ(Error::kNotSupported, error.type());
  EXPECT_EQ("service type is unsupported", error.message());
  EXPECT_EQ(nullptr, service);
}

TEST_F(ManagerTest, ConfigureServiceForProfileWithMissingProfile) {
  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  Error error;
  ServiceRefPtr service = manager()->ConfigureServiceForProfile(
      RpcIdentifier("/profile/foo"), args, &error);
  EXPECT_EQ(Error::kNotFound, error.type());
  EXPECT_EQ("Profile specified was not found", error.message());
  EXPECT_EQ(nullptr, service);
}

TEST_F(ManagerTest, ConfigureServiceForProfileWithProfileMismatch) {
  const RpcIdentifier kProfileName0("profile0");
  const RpcIdentifier kProfileName1("profile1");
  scoped_refptr<MockProfile> profile0(
      AddNamedMockProfileToManager(manager(), kProfileName0));

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kProfileProperty, kProfileName1);
  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName0, args, &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ(
      "Profile argument does not match that in "
      "the configuration arguments",
      error.message());
  EXPECT_EQ(nullptr, service);
}

TEST_F(ManagerTest,
       ConfigureServiceForProfileWithNoMatchingServiceFailGetService) {
  const RpcIdentifier kProfileName0("profile0");
  scoped_refptr<MockProfile> profile0(
      AddNamedMockProfileToManager(manager(), kProfileName0));
  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kProfileProperty, kProfileName0);

  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(WiFiServiceRefPtr()));
  EXPECT_CALL(*wifi_provider_, GetService(_, _))
      .WillOnce(Return(WiFiServiceRefPtr()));
  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName0, args, &error);
  // Since we didn't set the error in the GetService expectation above...
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(nullptr, service);
}

TEST_F(ManagerTest, ConfigureServiceForProfileCreateNewService) {
  const RpcIdentifier kProfileName0("profile0");
  scoped_refptr<MockProfile> profile0(
      AddNamedMockProfileToManager(manager(), kProfileName0));

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);

  scoped_refptr<MockWiFiService> mock_service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, vector<uint8_t>(), kModeManaged, kSecurityNone,
      false));
  ServiceRefPtr mock_service_generic(mock_service.get());
  mock_service->set_profile(profile0);
  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(WiFiServiceRefPtr()));
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).WillOnce(Return(mock_service));
  EXPECT_CALL(*profile0, UpdateService(mock_service_generic))
      .WillOnce(Return(true));
  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName0, args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(mock_service, service);
  mock_service->set_profile(nullptr);  // Breaks reference cycle.
}

TEST_F(ManagerTest, ConfigureServiceForProfileMatchingServiceByGUID) {
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  const string kGUID = "a guid";
  mock_service->SetGuid(kGUID, nullptr);
  manager()->RegisterService(mock_service);
  ServiceRefPtr mock_service_generic(mock_service.get());

  const RpcIdentifier kProfileName("profile");
  scoped_refptr<MockProfile> profile(
      AddNamedMockProfileToManager(manager(), kProfileName));
  mock_service->set_profile(profile);

  EXPECT_CALL(*mock_service, technology())
      .WillOnce(Return(Technology::kCellular))
      .WillOnce(Return(Technology::kWifi));

  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _)).Times(0);
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).Times(0);
  EXPECT_CALL(*profile, AdoptService(mock_service_generic)).Times(0);

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  args.SetString(kGuidProperty, kGUID);

  // The first attempt should fail because the service reports a technology
  // other than "WiFi".
  {
    Error error;
    ServiceRefPtr service =
        manager()->ConfigureServiceForProfile(kProfileName, args, &error);
    EXPECT_EQ(nullptr, service);
    EXPECT_EQ(Error::kNotSupported, error.type());
    EXPECT_EQ("This GUID matches a non-wifi service", error.message());
  }

  EXPECT_CALL(*mock_service, Configure(_, _)).Times(1);
  EXPECT_CALL(*profile, UpdateService(mock_service_generic)).Times(1);

  {
    Error error;
    ServiceRefPtr service =
        manager()->ConfigureServiceForProfile(kProfileName, args, &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(mock_service, service);
    EXPECT_EQ(profile, service->profile());
  }
  mock_service->set_profile(nullptr);  // Breaks reference cycle.
}

TEST_F(ManagerTest, ConfigureServiceForProfileMatchingServiceAndProfile) {
  const RpcIdentifier kProfileName("profile");
  scoped_refptr<MockProfile> profile(
      AddNamedMockProfileToManager(manager(), kProfileName));

  scoped_refptr<MockWiFiService> mock_service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, vector<uint8_t>(), kModeManaged, kSecurityNone,
      false));
  mock_service->set_profile(profile);
  ServiceRefPtr mock_service_generic(mock_service.get());

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(mock_service));
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).Times(0);
  EXPECT_CALL(*profile, AdoptService(mock_service_generic)).Times(0);
  EXPECT_CALL(*mock_service, Configure(_, _)).Times(1);
  EXPECT_CALL(*profile, UpdateService(mock_service_generic)).Times(1);

  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName, args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(mock_service, service);
  EXPECT_EQ(profile, service->profile());
  mock_service->set_profile(nullptr);  // Breaks reference cycle.
}

TEST_F(ManagerTest, ConfigureServiceForProfileMatchingServiceEphemeralProfile) {
  const RpcIdentifier kProfileName("profile");
  scoped_refptr<MockProfile> profile(
      AddNamedMockProfileToManager(manager(), kProfileName));

  scoped_refptr<MockWiFiService> mock_service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, vector<uint8_t>(), kModeManaged, kSecurityNone,
      false));
  mock_service->set_profile(GetEphemeralProfile(manager()));
  ServiceRefPtr mock_service_generic(mock_service.get());

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(mock_service));
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).Times(0);
  EXPECT_CALL(*mock_service, Configure(_, _)).Times(1);
  EXPECT_CALL(*profile, UpdateService(mock_service_generic)).Times(1);

  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName, args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(mock_service, service);
  EXPECT_EQ(profile, service->profile());
  mock_service->set_profile(nullptr);  // Breaks reference cycle.
}

TEST_F(ManagerTest, ConfigureServiceForProfileMatchingServicePrecedingProfile) {
  const RpcIdentifier kProfileName0("profile0");
  scoped_refptr<MockProfile> profile0(
      AddNamedMockProfileToManager(manager(), kProfileName0));
  const RpcIdentifier kProfileName1("profile1");
  scoped_refptr<MockProfile> profile1(
      AddNamedMockProfileToManager(manager(), kProfileName1));

  scoped_refptr<MockWiFiService> mock_service(new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, vector<uint8_t>(), kModeManaged, kSecurityNone,
      false));
  manager()->RegisterService(mock_service);
  mock_service->set_profile(profile0);
  ServiceRefPtr mock_service_generic(mock_service.get());

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(mock_service));
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).Times(0);
  EXPECT_CALL(*profile0, AbandonService(_)).Times(0);
  EXPECT_CALL(*profile1, AdoptService(_)).Times(0);
  // This happens once to make the service loadable for the ConfigureService
  // below, and a second time after the service is modified.
  EXPECT_CALL(*profile1, ConfigureService(mock_service_generic)).Times(0);
  EXPECT_CALL(*wifi_provider_, CreateTemporaryService(_, _)).Times(0);
  EXPECT_CALL(*mock_service, Configure(_, _)).Times(1);
  EXPECT_CALL(*profile1, UpdateService(mock_service_generic)).Times(1);

  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName1, args, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(mock_service, service);
  mock_service->set_profile(nullptr);  // Breaks reference cycle.
}

TEST_F(ManagerTest,
       ConfigureServiceForProfileMatchingServiceProceedingProfile) {
  const RpcIdentifier kProfileName0("profile0");
  scoped_refptr<MockProfile> profile0(
      AddNamedMockProfileToManager(manager(), kProfileName0));
  const RpcIdentifier kProfileName1("profile1");
  scoped_refptr<MockProfile> profile1(
      AddNamedMockProfileToManager(manager(), kProfileName1));

  scoped_refptr<MockWiFiService> matching_service(
      new StrictMock<MockWiFiService>(manager(), wifi_provider_,
                                      vector<uint8_t>(), kModeManaged,
                                      kSecurityNone, false));
  matching_service->set_profile(profile1);

  // We need to get rid of our reference to this mock service as soon
  // as Manager::ConfigureServiceForProfile() takes a reference in its
  // call to WiFiProvider::CreateTemporaryService().  This way the
  // latter function can keep a DCHECK(service->HasOneRef() even in
  // unit tests.
  temp_mock_service_ = new NiceMock<MockWiFiService>(
      manager(), wifi_provider_, vector<uint8_t>(), kModeManaged, kSecurityNone,
      false);

  // Only hold a pointer here so we don't affect the refcount.
  MockWiFiService* mock_service_ptr = temp_mock_service_.get();

  KeyValueStore args;
  args.SetString(kTypeProperty, kTypeWifi);
  EXPECT_CALL(*wifi_provider_, FindSimilarService(_, _))
      .WillOnce(Return(matching_service));
  EXPECT_CALL(*wifi_provider_, GetService(_, _)).Times(0);
  EXPECT_CALL(*profile1, AbandonService(_)).Times(0);
  EXPECT_CALL(*profile0, AdoptService(_)).Times(0);
  EXPECT_CALL(*wifi_provider_, CreateTemporaryService(_, _))
      .WillOnce(InvokeWithoutArgs(this, &ManagerTest::ReleaseTempMockService));
  EXPECT_CALL(*profile0, ConfigureService(IsRefPtrTo(mock_service_ptr)))
      .Times(1);
  EXPECT_CALL(*mock_service_ptr, Configure(_, _)).Times(1);
  EXPECT_CALL(*profile0, UpdateService(IsRefPtrTo(mock_service_ptr))).Times(1);

  Error error;
  ServiceRefPtr service =
      manager()->ConfigureServiceForProfile(kProfileName0, args, &error);
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(Error::kNotFound, error.type());
  EXPECT_EQ("Temporary service configured but not usable", error.message());
  EXPECT_EQ(nullptr, service);
  EXPECT_EQ(profile1, matching_service->profile());
}
#endif  // DISABLE_WIFI

TEST_F(ManagerTest, FindMatchingService) {
  KeyValueStore args;
  {
    Error error;
    ServiceRefPtr service = manager()->FindMatchingService(args, &error);
    EXPECT_EQ(Error::kNotFound, error.type());
  }

  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service0);
  manager()->RegisterService(mock_service1);
  EXPECT_CALL(*mock_service0, DoPropertiesMatch(_))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  {
    Error error;
    EXPECT_EQ(mock_service0, manager()->FindMatchingService(args, &error));
    EXPECT_TRUE(error.IsSuccess());
  }
  EXPECT_CALL(*mock_service1, DoPropertiesMatch(_))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  {
    Error error;
    EXPECT_EQ(mock_service1, manager()->FindMatchingService(args, &error));
    EXPECT_TRUE(error.IsSuccess());
  }
  {
    Error error;
    EXPECT_FALSE(manager()->FindMatchingService(args, &error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }
}

TEST_F(ManagerTest, TechnologyOrder) {
  // If the Manager is not running, setting the technology order should not
  // lauch a service sorting task.
  SetRunning(false);
  Error error;
  manager()->SetTechnologyOrder("vpn,ethernet,wifi,cellular", &error);
  ASSERT_TRUE(error.IsSuccess());
  EXPECT_FALSE(IsSortServicesTaskPending());
  EXPECT_THAT(GetTechnologyOrder(),
              ElementsAre(Technology::kVPN, Technology::kEthernet,
                          Technology::kWifi, Technology::kCellular));

  SetRunning(true);
  manager()->SetTechnologyOrder(string(kTypeEthernet) + "," + string(kTypeWifi),
                                &error);
  EXPECT_TRUE(IsSortServicesTaskPending());
  ASSERT_TRUE(error.IsSuccess());
  EXPECT_EQ(manager()->GetTechnologyOrder(),
            string(kTypeEthernet) + "," + string(kTypeWifi));

  manager()->SetTechnologyOrder(
      string(kTypeEthernet) + "x," + string(kTypeWifi), &error);
  ASSERT_FALSE(error.IsSuccess());
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ(string(kTypeEthernet) + "," + string(kTypeWifi),
            manager()->GetTechnologyOrder());
}

TEST_F(ManagerTest, ConnectionStatusCheck) {
  // Setup mock service.
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service);

  // Device not connected.
  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(false));
  EXPECT_CALL(*metrics(),
              NotifyDeviceConnectionStatus(Metrics::kConnectionStatusOffline));
  manager()->ConnectionStatusCheck();

  // Device connected, but not online.
  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(true));
  EXPECT_CALL(*mock_service, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(*metrics(),
              NotifyDeviceConnectionStatus(Metrics::kConnectionStatusOnline))
      .Times(0);
  EXPECT_CALL(*metrics(), NotifyDeviceConnectionStatus(
                              Metrics::kConnectionStatusConnected));
  manager()->ConnectionStatusCheck();

  // Device connected and online.
  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(true));
  EXPECT_CALL(*mock_service, IsOnline()).WillOnce(Return(true));
  EXPECT_CALL(*metrics(),
              NotifyDeviceConnectionStatus(Metrics::kConnectionStatusOnline));
  EXPECT_CALL(*metrics(), NotifyDeviceConnectionStatus(
                              Metrics::kConnectionStatusConnected));
  manager()->ConnectionStatusCheck();
}

TEST_F(ManagerTest, DevicePresenceStatusCheck) {
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kEthernet));

  EXPECT_CALL(*metrics(), NotifyDevicePresenceStatus(
                              Technology(Technology::kEthernet), true));
  EXPECT_CALL(*metrics(),
              NotifyDevicePresenceStatus(Technology(Technology::kWifi), true));
  EXPECT_CALL(*metrics(), NotifyDevicePresenceStatus(
                              Technology(Technology::kCellular), false));
  manager()->DevicePresenceStatusCheck();
}

TEST_F(ManagerTest, SortServicesWithConnection) {
  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));

  scoped_refptr<MockConnection> mock_connection0(
      new NiceMock<MockConnection>(device_info_.get()));
  scoped_refptr<MockConnection> mock_connection1(
      new NiceMock<MockConnection>(device_info_.get()));

  // A single registered Service, without a connection.  The
  // DefaultService should be nullptr.  If a change notification is
  // generated, it should reference kNullPath.
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_, EmitRpcIdentifierChanged(
                                     kDefaultServiceProperty,
                                     control_interface()->NullRpcIdentifier()))
      .Times(AnyNumber());
  manager()->RegisterService(mock_service0);
  CompleteServiceSort();

  // Adding another Service, also without a connection, does not
  // change DefaultService.  Furthermore, we do not send a change
  // notification for DefaultService.
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _))
      .Times(0);
  manager()->RegisterService(mock_service1);
  CompleteServiceSort();

  // An explicit sort doesn't change anything, and does not emit a
  // change notification for DefaultService.
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _))
      .Times(0);
  manager()->SortServicesTask();
  EXPECT_TRUE(ServiceOrderIs(mock_service0, mock_service1));

  // Re-ordering the unconnected Services doesn't change
  // DefaultService, and (hence) does not emit a change notification
  // for DefaultService.
  mock_service1->SetPriority(1, nullptr);
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _))
      .Times(0);
  manager()->SortServicesTask();
  EXPECT_TRUE(ServiceOrderIs(mock_service1, mock_service0));

  // Re-ordering the unconnected Services doesn't change
  // DefaultService, and (hence) does not emit a change notification
  // for DefaultService.
  mock_service1->SetPriority(0, nullptr);
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _))
      .Times(0);
  manager()->SortServicesTask();
  EXPECT_TRUE(ServiceOrderIs(mock_service0, mock_service1));

  mock_service0->set_mock_connection(mock_connection0);
  mock_service1->set_mock_connection(mock_connection1);

  // Add an entry to the dns_servers() list to test the logic in
  // SortServicesTask() which figures out which connection owns the system
  // DNS configuration.
  std::vector<std::string> dns_servers;
  dns_servers.push_back("8.8.8.8");
  EXPECT_CALL(*mock_connection0, dns_servers())
      .WillRepeatedly(ReturnRef(dns_servers));
  EXPECT_CALL(*mock_connection1, dns_servers())
      .WillRepeatedly(ReturnRef(dns_servers));

  // If both Services have Connections, the DefaultService follows
  // from ServiceOrderIs.  We notify others of the change in
  // DefaultService.
  EXPECT_CALL(*mock_connection0, SetUseDNS(true));
  EXPECT_CALL(*mock_connection0, SetMetric(Connection::kDefaultMetric +
                                               Connection::kMetricIncrement,
                                           true));
  EXPECT_CALL(*mock_connection0, SetMetric(Connection::kDefaultMetric, true));
  EXPECT_CALL(*mock_connection1, SetUseDNS(false));
  EXPECT_CALL(*mock_connection1, SetMetric(Connection::kDefaultMetric +
                                               2 * Connection::kMetricIncrement,
                                           false));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(mock_service0.get()));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _));
  manager()->SortServicesTask();
  EXPECT_TRUE(ServiceOrderIs(mock_service0, mock_service1));

  ServiceWatcher service_watcher;
  manager()->AddDefaultServiceObserver(&service_watcher);

  // Changing the ordering causes the DefaultService to change, and
  // appropriate notifications are sent.
  mock_service1->SetPriority(1, nullptr);
  EXPECT_CALL(*mock_connection0, SetUseDNS(false));
  EXPECT_CALL(*mock_connection0, SetMetric(Connection::kDefaultMetric +
                                               2 * Connection::kMetricIncrement,
                                           false));
  EXPECT_CALL(*mock_connection1, SetUseDNS(true));
  EXPECT_CALL(*mock_connection1, SetMetric(Connection::kDefaultMetric +
                                               Connection::kMetricIncrement,
                                           true));
  EXPECT_CALL(*mock_connection1, SetMetric(Connection::kDefaultMetric, true));
  EXPECT_CALL(service_watcher, OnDefaultServiceChanged(_, _, _, _));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(mock_service1.get()));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _));
  manager()->SortServicesTask();
  EXPECT_TRUE(ServiceOrderIs(mock_service1, mock_service0));

  // Deregistering a DefaultServiceCallback works as expected.  (Later
  // code causes DefaultService changes, but we see no further calls
  // to |service_watcher|.)
  manager()->RemoveDefaultServiceObserver(&service_watcher);
  EXPECT_CALL(service_watcher, OnDefaultServiceChanged(_, _, _, _)).Times(0);

  // Deregistering the current DefaultService causes the other Service
  // to become default.  Appropriate notifications are sent.
  EXPECT_CALL(*mock_connection0, SetUseDNS(true));
  EXPECT_CALL(*mock_connection0, SetMetric(Connection::kDefaultMetric +
                                               Connection::kMetricIncrement,
                                           true));
  EXPECT_CALL(*mock_connection0, SetMetric(Connection::kDefaultMetric, true));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(mock_service0.get()));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _));
  mock_service1->set_mock_connection(nullptr);  // So DeregisterService works.
  manager()->DeregisterService(mock_service1);
  CompleteServiceSort();

  // Deregistering the only Service causes the DefaultService to become
  // nullptr.  Appropriate notifications are sent.
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _));
  mock_service0->set_mock_connection(nullptr);  // So DeregisterService works.
  manager()->DeregisterService(mock_service0);
  CompleteServiceSort();

  // An explicit sort doesn't change anything, and does not generate
  // an external notification.
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierChanged(kDefaultServiceProperty, _))
      .Times(0);
  manager()->SortServicesTask();
}

TEST_F(ManagerTest, UpdateDefaultServices) {
  EXPECT_EQ(GetDefaultServiceObserverCount(), 0);

  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  ServiceRefPtr service = mock_service;
  ServiceRefPtr null_service = nullptr;

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(null_service, null_service);

  ServiceWatcher service_watcher1;
  ServiceWatcher service_watcher2;
  manager()->AddDefaultServiceObserver(&service_watcher1);
  manager()->AddDefaultServiceObserver(&service_watcher2);

  EXPECT_CALL(service_watcher1,
              OnDefaultServiceChanged(service, false, service, true));
  EXPECT_CALL(service_watcher2,
              OnDefaultServiceChanged(service, false, service, true));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(service.get()));
  manager()->UpdateDefaultServices(mock_service, mock_service);

  EXPECT_CALL(service_watcher1,
              OnDefaultServiceChanged(null_service, false, null_service, true));
  EXPECT_CALL(service_watcher2,
              OnDefaultServiceChanged(null_service, false, null_service, true));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(null_service, null_service);

  manager()->RemoveDefaultServiceObserver(&service_watcher1);
  EXPECT_CALL(service_watcher1, OnDefaultServiceChanged(_, _, _, _)).Times(0);
  EXPECT_CALL(service_watcher2,
              OnDefaultServiceChanged(service, false, service, true));
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(service.get()));
  manager()->UpdateDefaultServices(mock_service, mock_service);
  EXPECT_EQ(GetDefaultServiceObserverCount(), 1);

  manager()->RemoveDefaultServiceObserver(&service_watcher2);
  EXPECT_CALL(service_watcher2, OnDefaultServiceChanged(_, _, _, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(null_service, null_service);

  EXPECT_EQ(GetDefaultServiceObserverCount(), 0);
}

TEST_F(ManagerTest, UpdateDefaultServicesWithDefaultServiceCallbacksRemoved) {
  EXPECT_EQ(GetDefaultServiceObserverCount(), 0);

  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  ServiceRefPtr service = mock_service;
  ServiceRefPtr null_service = nullptr;

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(null_service, null_service);

  // Register many callbacks where each callback simply deregisters itself from
  // Manager. This verifies that Manager::UpdateDefaultServices() can safely
  // iterate the container holding the callbacks while callbacks are removed
  // from the container during iteration.
  ServiceWatcher service_watchers[1000];
  for (auto& service_watcher : service_watchers) {
    manager()->AddDefaultServiceObserver(&service_watcher);
    EXPECT_CALL(service_watcher,
                OnDefaultServiceChanged(service, false, service, true))
        .WillOnce(Invoke([this, &service_watcher](const ServiceRefPtr&, bool,
                                                  const ServiceRefPtr&, bool) {
          manager()->RemoveDefaultServiceObserver(&service_watcher);
        }));
  }

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(service.get()));
  manager()->UpdateDefaultServices(mock_service, mock_service);
  EXPECT_EQ(GetDefaultServiceObserverCount(), 0);

  for (auto& service_watcher : service_watchers) {
    EXPECT_CALL(service_watcher, OnDefaultServiceChanged(_, _, _, _)).Times(0);
  }
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(null_service, null_service);
  EXPECT_EQ(GetDefaultServiceObserverCount(), 0);
}

TEST_F(ManagerTest, DefaultServiceStateChange) {
  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));

  manager()->RegisterService(mock_service0);
  manager()->RegisterService(mock_service1);

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(mock_service0.get()));
  manager()->UpdateDefaultServices(mock_service0, mock_service0);

  // Changing the default service's state should notify both services.
  EXPECT_CALL(*mock_service0, OnDefaultServiceStateChanged(_));
  EXPECT_CALL(*mock_service1, OnDefaultServiceStateChanged(_));
  manager()->NotifyServiceStateChanged(mock_service0);
  Mock::VerifyAndClearExpectations(mock_service0.get());
  Mock::VerifyAndClearExpectations(mock_service1.get());

  // Changing the non-default service's state shouldn't notify anyone.
  EXPECT_CALL(*mock_service0, OnDefaultServiceStateChanged(_)).Times(0);
  EXPECT_CALL(*mock_service1, OnDefaultServiceStateChanged(_)).Times(0);
  manager()->NotifyServiceStateChanged(mock_service1);

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(nullptr));
  manager()->UpdateDefaultServices(nullptr, nullptr);

  manager()->DeregisterService(mock_service1);
  manager()->DeregisterService(mock_service0);
}

TEST_F(ManagerTest, ReportServicesOnSameNetwork) {
  int connection_id1 = 100;
  int connection_id2 = 200;
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));
  mock_service1->set_connection_id(connection_id1);
  MockServiceRefPtr mock_service2 = new NiceMock<MockService>(manager());
  mock_service2->set_connection_id(connection_id1);
  MockServiceRefPtr mock_service3 = new NiceMock<MockService>(manager());
  mock_service3->set_connection_id(connection_id2);

  manager()->RegisterService(mock_service1);
  manager()->RegisterService(mock_service2);
  manager()->RegisterService(mock_service3);

  EXPECT_CALL(*metrics(), NotifyServicesOnSameNetwork(2));
  manager()->ReportServicesOnSameNetwork(connection_id1);

  EXPECT_CALL(*metrics(), NotifyServicesOnSameNetwork(1));
  manager()->ReportServicesOnSameNetwork(connection_id2);
}

TEST_F(ManagerTest, AvailableTechnologies) {
  mock_devices_.push_back(
      new NiceMock<MockDevice>(manager(), "null4", "addr4", 0));
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);
  manager()->RegisterDevice(mock_devices_[3]);

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));
  ON_CALL(*mock_devices_[3], technology())
      .WillByDefault(Return(Technology::kWifi));

  set<string> expected_technologies;
  expected_technologies.insert(Technology(Technology::kEthernet).GetName());
  expected_technologies.insert(Technology(Technology::kWifi).GetName());
  expected_technologies.insert(Technology(Technology::kCellular).GetName());
  Error error;
  vector<string> technologies = manager()->AvailableTechnologies(&error);

  EXPECT_THAT(set<string>(technologies.begin(), technologies.end()),
              ContainerEq(expected_technologies));
}

TEST_F(ManagerTest, ConnectedTechnologies) {
  MockServiceRefPtr connected_service1(new NiceMock<MockService>(manager()));
  MockServiceRefPtr connected_service2(new NiceMock<MockService>(manager()));
  MockServiceRefPtr disconnected_service1(new NiceMock<MockService>(manager()));
  MockServiceRefPtr disconnected_service2(new NiceMock<MockService>(manager()));

  ON_CALL(*connected_service1, IsConnected()).WillByDefault(Return(true));
  ON_CALL(*connected_service2, IsConnected()).WillByDefault(Return(true));

  manager()->RegisterService(connected_service1);
  manager()->RegisterService(connected_service2);
  manager()->RegisterService(disconnected_service1);
  manager()->RegisterService(disconnected_service2);

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);
  manager()->RegisterDevice(mock_devices_[3]);

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kWifi));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));
  ON_CALL(*mock_devices_[3], technology())
      .WillByDefault(Return(Technology::kWifi));

  mock_devices_[0]->SelectService(connected_service1);
  mock_devices_[1]->SelectService(disconnected_service1);
  mock_devices_[2]->SelectService(disconnected_service2);
  mock_devices_[3]->SelectService(connected_service2);

  set<string> expected_technologies;
  expected_technologies.insert(Technology(Technology::kEthernet).GetName());
  expected_technologies.insert(Technology(Technology::kWifi).GetName());
  Error error;

  vector<string> technologies = manager()->ConnectedTechnologies(&error);
  EXPECT_THAT(set<string>(technologies.begin(), technologies.end()),
              ContainerEq(expected_technologies));
}

TEST_F(ManagerTest, DefaultTechnology) {
  MockServiceRefPtr connected_service(new NiceMock<MockService>(manager()));
  MockServiceRefPtr disconnected_service(new NiceMock<MockService>(manager()));

  // Connected. WiFi.
  ON_CALL(*connected_service, IsConnected()).WillByDefault(Return(true));
  ON_CALL(*connected_service, state())
      .WillByDefault(Return(Service::kStateConnected));
  ON_CALL(*connected_service, technology())
      .WillByDefault(Return(Technology::kWifi));

  // Disconnected. Ethernet.
  ON_CALL(*disconnected_service, technology())
      .WillByDefault(Return(Technology::kEthernet));

  manager()->RegisterService(disconnected_service);
  CompleteServiceSort();
  Error error;
  EXPECT_THAT(manager()->DefaultTechnology(&error), StrEq(""));

  manager()->RegisterService(connected_service);
  CompleteServiceSort();
  // Connected service should be brought to the front now.
  string expected_technology = Technology(Technology::kWifi).GetName();
  EXPECT_THAT(manager()->DefaultTechnology(&error), StrEq(expected_technology));
}

TEST_F(ManagerTest, Stop) {
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);
  MockServiceRefPtr service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(service);
  manager()->RegisterDevice(mock_devices_[0]);
  SetPowerManager();
  EXPECT_TRUE(manager()->power_manager());
  EXPECT_CALL(*profile, UpdateDevice(DeviceRefPtr(mock_devices_[0].get())))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_devices_[0], SetEnabled(false));
#if !defined(DISABLE_WIFI)
  EXPECT_CALL(*profile, UpdateWiFiProvider(_)).WillOnce(Return(true));
#endif  // DISABLE_WIFI
  EXPECT_CALL(*profile, Save()).WillOnce(Return(true));
  EXPECT_CALL(*service, Disconnect(_, HasSubstr("Stop"))).Times(1);
  manager()->Stop();
  EXPECT_FALSE(manager()->power_manager());
}

TEST_F(ManagerTest, UpdateServiceConnected) {
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service);
  EXPECT_FALSE(mock_service->retain_auto_connect());
  EXPECT_FALSE(mock_service->auto_connect());

  EXPECT_CALL(*mock_service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_service, EnableAndRetainAutoConnect());
  manager()->UpdateService(mock_service);
}

TEST_F(ManagerTest, UpdateServiceConnectedPersistAutoConnect) {
  // This tests the case where the user connects to a service that is
  // currently associated with a profile.  We want to make sure that the
  // auto_connect flag is set and that the is saved to the current profile.
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service);
  EXPECT_FALSE(mock_service->retain_auto_connect());
  EXPECT_FALSE(mock_service->auto_connect());

  scoped_refptr<MockProfile> profile(new MockProfile(manager(), ""));

  mock_service->set_profile(profile);
  EXPECT_CALL(*mock_service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*profile,
              UpdateService(static_cast<ServiceRefPtr>(mock_service)));
  EXPECT_CALL(*mock_service, EnableAndRetainAutoConnect());
  manager()->UpdateService(mock_service);
  // This releases the ref on the mock profile.
  mock_service->set_profile(nullptr);
}

TEST_F(ManagerTest, UpdateServiceLogging) {
  ScopedMockLog log;
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  string updated_message = base::StringPrintf(
      "Service %s updated;", mock_service->unique_name().c_str());

  // An idle service should only be logged as unconnected.
  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("not connected")));
  manager()->RegisterService(mock_service);
  CompleteServiceSort();
  manager()->UpdateService(mock_service);
  CompleteServiceSort();
  Mock::VerifyAndClearExpectations(mock_service.get());
  Mock::VerifyAndClearExpectations(&log);

  // A service leaving the idle state should create a log message.
  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateAssociating));
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr(updated_message)))
      .Times(1);
  manager()->UpdateService(mock_service.get());
  CompleteServiceSort();
  Mock::VerifyAndClearExpectations(&log);

  // A service in a non-idle state should not create a log message if its
  // state did not change.
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr(updated_message)))
      .Times(0);
  manager()->UpdateService(mock_service);
  CompleteServiceSort();
  Mock::VerifyAndClearExpectations(mock_service.get());
  Mock::VerifyAndClearExpectations(&log);

  // A service transitioning between two non-idle states should create
  // a log message.
  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr(updated_message)))
      .Times(1);
  manager()->UpdateService(mock_service.get());
  CompleteServiceSort();
  Mock::VerifyAndClearExpectations(mock_service.get());
  Mock::VerifyAndClearExpectations(&log);

  // A service transitioning from a non-idle state to idle should create
  // a log message.
  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr(updated_message)))
      .Times(1);
  manager()->UpdateService(mock_service.get());
  CompleteServiceSort();
}

TEST_F(ManagerTest, SaveSuccessfulService) {
  scoped_refptr<MockProfile> profile(
      new StrictMock<MockProfile>(manager(), ""));
  AdoptProfile(manager(), profile);
  MockServiceRefPtr service(new NiceMock<MockService>(manager()));

  // Re-cast this back to a ServiceRefPtr, so EXPECT arguments work correctly.
  ServiceRefPtr expect_service(service.get());

  EXPECT_CALL(*profile, ConfigureService(expect_service))
      .WillOnce(Return(false));
  manager()->RegisterService(service);

  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*profile, AdoptService(expect_service)).WillOnce(Return(true));
  manager()->UpdateService(service);
}

TEST_F(ManagerTest, UpdateDevice) {
  MockProfile* profile0 = new MockProfile(manager(), "");
  MockProfile* profile1 = new MockProfile(manager(), "");
  MockProfile* profile2 = new MockProfile(manager(), "");
  AdoptProfile(manager(), profile0);  // Passes ownership.
  AdoptProfile(manager(), profile1);  // Passes ownership.
  AdoptProfile(manager(), profile2);  // Passes ownership.
  DeviceRefPtr device_ref(mock_devices_[0].get());
  EXPECT_CALL(*profile0, UpdateDevice(device_ref)).Times(0);
  EXPECT_CALL(*profile1, UpdateDevice(device_ref)).WillOnce(Return(true));
  EXPECT_CALL(*profile2, UpdateDevice(device_ref)).WillOnce(Return(false));
  manager()->UpdateDevice(mock_devices_[0]);
}

TEST_F(ManagerTest, EnumerateProfiles) {
  vector<RpcIdentifier> profile_paths;
  for (size_t i = 0; i < 10; i++) {
    scoped_refptr<MockProfile> profile(
        new StrictMock<MockProfile>(manager(), ""));
    profile_paths.push_back(
        RpcIdentifier(base::StringPrintf("/profile/%zd", i)));
    EXPECT_CALL(*profile, GetRpcIdentifier())
        .WillOnce(Return(profile_paths.back()));
    AdoptProfile(manager(), profile);
  }

  Error error;
  vector<RpcIdentifier> returned_paths = manager()->EnumerateProfiles(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(profile_paths.size(), returned_paths.size());
  for (size_t i = 0; i < profile_paths.size(); i++) {
    EXPECT_EQ(profile_paths[i], returned_paths[i]);
  }
}

TEST_F(ManagerTest, EnumerateServiceInnerDevices) {
  MockServiceRefPtr service1(new NiceMock<MockService>(manager()));
  MockServiceRefPtr service2(new NiceMock<MockService>(manager()));
  const RpcIdentifier kDeviceRpcID("/rpc/");
  manager()->RegisterService(service1);
  manager()->RegisterService(service2);
  EXPECT_CALL(*service1, GetInnerDeviceRpcIdentifier())
      .WillRepeatedly(Return(kDeviceRpcID));
  EXPECT_CALL(*service2, GetInnerDeviceRpcIdentifier())
      .WillRepeatedly(Return(RpcIdentifier("")));
  Error error;
  EXPECT_EQ(vector<RpcIdentifier>{kDeviceRpcID},
            manager()->EnumerateDevices(&error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ManagerTest, AutoConnectOnRegister) {
  MockServiceRefPtr service = MakeAutoConnectableService();
  EXPECT_CALL(*service, AutoConnect());
  manager()->RegisterService(service);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, AutoConnectOnUpdate) {
  MockServiceRefPtr service1 = MakeAutoConnectableService();
  service1->SetPriority(1, nullptr);
  MockServiceRefPtr service2 = MakeAutoConnectableService();
  service2->SetPriority(2, nullptr);
  manager()->RegisterService(service1);
  manager()->RegisterService(service2);
  dispatcher()->DispatchPendingEvents();

  EXPECT_CALL(*service1, AutoConnect());
  EXPECT_CALL(*service2, state())
      .WillRepeatedly(Return(Service::kStateFailure));
  EXPECT_CALL(*service2, IsFailed()).WillRepeatedly(Return(true));
  EXPECT_CALL(*service2, IsConnected()).WillRepeatedly(Return(false));
  manager()->UpdateService(service2);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, AutoConnectOnDeregister) {
  MockServiceRefPtr service1 = MakeAutoConnectableService();
  service1->SetPriority(1, nullptr);
  MockServiceRefPtr service2 = MakeAutoConnectableService();
  service2->SetPriority(2, nullptr);
  manager()->RegisterService(service1);
  manager()->RegisterService(service2);
  dispatcher()->DispatchPendingEvents();

  EXPECT_CALL(*service1, AutoConnect());
  manager()->DeregisterService(service2);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, AutoConnectOnSuspending) {
  MockServiceRefPtr service = MakeAutoConnectableService();
  SetSuspending(true);
  SetPowerManager();
  EXPECT_CALL(*service, AutoConnect()).Times(0);
  manager()->RegisterService(service);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, AutoConnectOnNotSuspending) {
  MockServiceRefPtr service = MakeAutoConnectableService();
  SetSuspending(false);
  SetPowerManager();
  EXPECT_CALL(*service, AutoConnect());
  manager()->RegisterService(service);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, AutoConnectWhileNotRunning) {
  SetRunning(false);
  MockServiceRefPtr service = MakeAutoConnectableService();
  EXPECT_CALL(*service, AutoConnect()).Times(0);
  manager()->RegisterService(service);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, Suspend) {
  MockServiceRefPtr service = MakeAutoConnectableService();
  SetPowerManager();
  EXPECT_CALL(*service, AutoConnect());
  manager()->RegisterService(service);
  manager()->RegisterDevice(mock_devices_[0]);
  dispatcher()->DispatchPendingEvents();

  EXPECT_CALL(*mock_devices_[0], OnBeforeSuspend(_));
  EXPECT_CALL(*service, OnBeforeSuspend(_));
  OnSuspendImminent();
  EXPECT_CALL(*service, AutoConnect()).Times(0);
  dispatcher()->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(mock_devices_[0].get());

  EXPECT_CALL(*mock_devices_[0], OnAfterResume());
  EXPECT_CALL(*service, OnAfterResume());
  OnSuspendDone();
  EXPECT_CALL(*service, AutoConnect());
  dispatcher()->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(mock_devices_[0].get());
}

TEST_F(ManagerTest, AddTerminationAction) {
  EXPECT_TRUE(GetTerminationActions()->IsEmpty());
  manager()->AddTerminationAction("action1", base::Closure());
  EXPECT_FALSE(GetTerminationActions()->IsEmpty());
  manager()->AddTerminationAction("action2", base::Closure());
}

TEST_F(ManagerTest, RemoveTerminationAction) {
  const char kKey1[] = "action1";
  const char kKey2[] = "action2";

  // Removing an action when the hook table is empty.
  EXPECT_TRUE(GetTerminationActions()->IsEmpty());
  manager()->RemoveTerminationAction("unknown");

  // Fill hook table with two items.
  manager()->AddTerminationAction(kKey1, base::Closure());
  EXPECT_FALSE(GetTerminationActions()->IsEmpty());
  manager()->AddTerminationAction(kKey2, base::Closure());

  // Removing an action that ends up with a non-empty hook table.
  manager()->RemoveTerminationAction(kKey1);
  EXPECT_FALSE(GetTerminationActions()->IsEmpty());

  // Removing the last action.
  manager()->RemoveTerminationAction(kKey2);
  EXPECT_TRUE(GetTerminationActions()->IsEmpty());
}

TEST_F(ManagerTest, RunTerminationActions) {
  TerminationActionTest test_action;
  const string kActionName = "action";

  EXPECT_CALL(test_action, Done(_));
  manager()->RunTerminationActions(
      Bind(&TerminationActionTest::Done, test_action.AsWeakPtr()));

  manager()->AddTerminationAction(
      TerminationActionTest::kActionName,
      Bind(&TerminationActionTest::Action, test_action.AsWeakPtr()));
  test_action.set_manager(manager());
  EXPECT_CALL(test_action, Done(_));
  manager()->RunTerminationActions(
      Bind(&TerminationActionTest::Done, test_action.AsWeakPtr()));
}

TEST_F(ManagerTest, OnSuspendImminentDevicesPresent) {
  EXPECT_CALL(*mock_devices_[0], OnBeforeSuspend(_));
  EXPECT_CALL(*mock_devices_[1], OnBeforeSuspend(_));
  EXPECT_CALL(*mock_devices_[2], OnBeforeSuspend(_));
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);
  SetPowerManager();
  OnSuspendImminent();
}

TEST_F(ManagerTest, OnSuspendImminentNoDevicesPresent) {
  EXPECT_CALL(*power_manager_, ReportSuspendReadiness());
  SetPowerManager();
  OnSuspendImminent();
}

TEST_F(ManagerTest, OnDarkSuspendImminentDevicesPresent) {
  EXPECT_CALL(*mock_devices_[0], OnDarkResume(_));
  EXPECT_CALL(*mock_devices_[1], OnDarkResume(_));
  EXPECT_CALL(*mock_devices_[2], OnDarkResume(_));
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);
  SetPowerManager();
  OnDarkSuspendImminent();
}

TEST_F(ManagerTest, OnDarkSuspendImminentNoDevicesPresent) {
  EXPECT_CALL(*power_manager_, ReportDarkSuspendReadiness());
  SetPowerManager();
  OnDarkSuspendImminent();
}

TEST_F(ManagerTest, OnSuspendActionsComplete) {
  Error error;
  EXPECT_CALL(*power_manager_, ReportSuspendReadiness());
  SetPowerManager();
  OnSuspendActionsComplete(error);
}

TEST_F(ManagerTest, RecheckPortal) {
  EXPECT_CALL(*mock_devices_[0], RequestPortalDetection())
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_devices_[1], RequestPortalDetection())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_devices_[2], RequestPortalDetection()).Times(0);

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  manager()->RecheckPortal(nullptr);
}

TEST_F(ManagerTest, RecheckPortalOnService) {
  MockServiceRefPtr service = new NiceMock<MockService>(manager());
  EXPECT_CALL(*mock_devices_[0], IsConnectedToService(IsRefPtrTo(service)))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_devices_[1], IsConnectedToService(IsRefPtrTo(service)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_devices_[1], RestartPortalDetection())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_devices_[2], IsConnectedToService(_)).Times(0);

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  manager()->RecheckPortalOnService(service);
}

TEST_F(ManagerTest, GetDefaultService) {
  EXPECT_EQ(nullptr, manager()->GetDefaultService());
  EXPECT_EQ(control_interface()->NullRpcIdentifier(),
            GetDefaultServiceRpcIdentifier());

  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service);
  EXPECT_EQ(nullptr, manager()->GetDefaultService());
  EXPECT_EQ(control_interface()->NullRpcIdentifier(),
            GetDefaultServiceRpcIdentifier());

  scoped_refptr<MockConnection> mock_connection(
      new NiceMock<MockConnection>(device_info_.get()));
  mock_service->set_mock_connection(mock_connection);
  EXPECT_EQ(mock_service, manager()->GetDefaultService());
  EXPECT_EQ(mock_service->GetRpcIdentifier(), GetDefaultServiceRpcIdentifier());

  mock_service->set_mock_connection(nullptr);
  manager()->DeregisterService(mock_service);
}

TEST_F(ManagerTest, GetServiceWithGUID) {
  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*mock_service0, Configure(_, _)).Times(0);
  EXPECT_CALL(*mock_service1, Configure(_, _)).Times(0);

  manager()->RegisterService(mock_service0);
  manager()->RegisterService(mock_service1);

  const string kGUID0 = "GUID0";
  const string kGUID1 = "GUID1";

  {
    Error error;
    ServiceRefPtr service = manager()->GetServiceWithGUID(kGUID0, &error);
    EXPECT_FALSE(error.IsSuccess());
    EXPECT_FALSE(service);
  }

  KeyValueStore args;
  args.SetString(kGuidProperty, kGUID1);

  {
    Error error;
    ServiceRefPtr service = manager()->GetService(args, &error);
    EXPECT_EQ(Error::kInvalidArguments, error.type());
    EXPECT_FALSE(service);
  }

  mock_service0->SetGuid(kGUID0, nullptr);
  mock_service1->SetGuid(kGUID1, nullptr);

  {
    Error error;
    ServiceRefPtr service = manager()->GetServiceWithGUID(kGUID0, &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(mock_service0, service);
  }

  {
    Error error;
    EXPECT_CALL(*mock_service1, Configure(_, &error)).Times(1);
    ServiceRefPtr service = manager()->GetService(args, &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(mock_service1, service);
  }

  manager()->DeregisterService(mock_service0);
  manager()->DeregisterService(mock_service1);
}

TEST_F(ManagerTest, CalculateStateOffline) {
  EXPECT_FALSE(manager()->IsConnected());
  EXPECT_EQ("offline", manager()->CalculateState(nullptr));

  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(_)).Times(AnyNumber());

  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*mock_service0, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_service1, IsConnected()).WillRepeatedly(Return(false));

  manager()->RegisterService(mock_service0);
  manager()->RegisterService(mock_service1);

  EXPECT_FALSE(manager()->IsConnected());
  EXPECT_EQ("offline", manager()->CalculateState(nullptr));

  manager()->DeregisterService(mock_service0);
  manager()->DeregisterService(mock_service1);
}

TEST_F(ManagerTest, CalculateStateOnline) {
  EXPECT_CALL(*metrics(), NotifyDefaultServiceChanged(_)).Times(AnyNumber());

  MockServiceRefPtr mock_service0(new NiceMock<MockService>(manager()));
  MockServiceRefPtr mock_service1(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*mock_service0, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_service1, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_service0, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*mock_service1, state())
      .WillRepeatedly(Return(Service::kStateConnected));

  manager()->RegisterService(mock_service0);
  manager()->RegisterService(mock_service1);
  CompleteServiceSort();

  EXPECT_TRUE(manager()->IsConnected());
  EXPECT_EQ("online", manager()->CalculateState(nullptr));

  manager()->DeregisterService(mock_service0);
  manager()->DeregisterService(mock_service1);
}

TEST_F(ManagerTest, RefreshConnectionState) {
  EXPECT_CALL(*manager_adaptor_,
              EmitStringChanged(kConnectionStateProperty, kStateIdle));
  EXPECT_CALL(*upstart_, NotifyDisconnected());
  EXPECT_CALL(*upstart_, NotifyConnected()).Times(0);
  RefreshConnectionState();
  Mock::VerifyAndClearExpectations(manager_adaptor_);
  Mock::VerifyAndClearExpectations(upstart_);

  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*manager_adaptor_, EmitStringChanged(kConnectionStateProperty, _))
      .Times(0);
  EXPECT_CALL(*upstart_, NotifyDisconnected()).Times(0);
  EXPECT_CALL(*upstart_, NotifyConnected());
  manager()->RegisterService(mock_service);
  RefreshConnectionState();

  scoped_refptr<MockConnection> mock_connection(
      new NiceMock<MockConnection>(device_info_.get()));
  mock_service->set_mock_connection(mock_connection);
  EXPECT_CALL(*mock_service, state()).WillOnce(Return(Service::kStateIdle));
  RefreshConnectionState();

  Mock::VerifyAndClearExpectations(manager_adaptor_);
  EXPECT_CALL(*mock_service, state())
      .WillOnce(Return(Service::kStateNoConnectivity));
  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(true));
  EXPECT_CALL(*manager_adaptor_, EmitStringChanged(kConnectionStateProperty,
                                                   kStateNoConnectivity));
  RefreshConnectionState();
  Mock::VerifyAndClearExpectations(manager_adaptor_);
  Mock::VerifyAndClearExpectations(upstart_);

  mock_service->set_mock_connection(nullptr);
  manager()->DeregisterService(mock_service);

  EXPECT_CALL(*manager_adaptor_,
              EmitStringChanged(kConnectionStateProperty, kStateIdle));
  EXPECT_CALL(*upstart_, NotifyDisconnected());
  EXPECT_CALL(*upstart_, NotifyConnected()).Times(0);
  RefreshConnectionState();
}

TEST_F(ManagerTest, StartupPortalList) {
  // Simulate loading value from the default profile.
  const string kProfileValue("wifi,vpn");
  manager()->props_.check_portal_list = kProfileValue;

  EXPECT_EQ(kProfileValue, manager()->GetCheckPortalList(nullptr));
  EXPECT_TRUE(manager()->IsPortalDetectionEnabled(Technology::kWifi));
  EXPECT_FALSE(manager()->IsPortalDetectionEnabled(Technology::kCellular));

  const string kStartupValue("cellular,ethernet");
  manager()->SetStartupPortalList(kStartupValue);
  // Ensure profile value is not overwritten, so when we save the default
  // profile, the correct value will still be written.
  EXPECT_EQ(kProfileValue, manager()->props_.check_portal_list);

  // However we should read back a different list.
  EXPECT_EQ(kStartupValue, manager()->GetCheckPortalList(nullptr));
  EXPECT_FALSE(manager()->IsPortalDetectionEnabled(Technology::kWifi));
  EXPECT_TRUE(manager()->IsPortalDetectionEnabled(Technology::kCellular));

  const string kRuntimeValue("ppp");
  // Setting a runtime value over the control API should overwrite both
  // the profile value and what we read back.
  Error error;
  manager()->mutable_store()->SetStringProperty(kCheckPortalListProperty,
                                                kRuntimeValue, &error);
  ASSERT_TRUE(error.IsSuccess());
  EXPECT_EQ(kRuntimeValue, manager()->GetCheckPortalList(nullptr));
  EXPECT_EQ(kRuntimeValue, manager()->props_.check_portal_list);
  EXPECT_FALSE(manager()->IsPortalDetectionEnabled(Technology::kCellular));
  EXPECT_TRUE(manager()->IsPortalDetectionEnabled(Technology::kPPP));
}

TEST_F(ManagerTest, LinkMonitorEnabled) {
  const string kEnabledTechnologies("wifi,vpn");
  manager()->props_.link_monitor_technologies = kEnabledTechnologies;
  EXPECT_TRUE(manager()->IsTechnologyLinkMonitorEnabled(Technology::kWifi));
  EXPECT_FALSE(
      manager()->IsTechnologyLinkMonitorEnabled(Technology::kCellular));
}

TEST_F(ManagerTest, IsTechnologyAutoConnectDisabled) {
  const string kNoAutoConnectTechnologies("wifi,cellular");
  manager()->props_.no_auto_connect_technologies = kNoAutoConnectTechnologies;
  EXPECT_TRUE(manager()->IsTechnologyAutoConnectDisabled(Technology::kWifi));
  EXPECT_TRUE(
      manager()->IsTechnologyAutoConnectDisabled(Technology::kCellular));
  EXPECT_FALSE(
      manager()->IsTechnologyAutoConnectDisabled(Technology::kEthernet));
}

TEST_F(ManagerTest, SetEnabledStateForTechnologyPersistentCheck) {
  Error error(Error::kOperationInitiated);
  DisableTechnologyReplyHandler disable_technology_reply_handler;
  ResultCallback disable_technology_callback(
      Bind(&DisableTechnologyReplyHandler::ReportResult,
           disable_technology_reply_handler.AsWeakPtr()));
  EXPECT_CALL(disable_technology_reply_handler, ReportResult(_)).Times(0);
  EXPECT_CALL(*mock_devices_[0], SetEnabledPersistent(false, _, _));

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  manager()->RegisterDevice(mock_devices_[0]);
  manager()->SetEnabledStateForTechnology(kTypeEthernet, false, true, &error,
                                          disable_technology_callback);

  EXPECT_CALL(*mock_devices_[0], SetEnabledNonPersistent(false, _, _));
  manager()->SetEnabledStateForTechnology(kTypeEthernet, false, false, &error,
                                          disable_technology_callback);
}

TEST_F(ManagerTest, SetEnabledStateForTechnology) {
  Error error(Error::kOperationInitiated);
  DisableTechnologyReplyHandler disable_technology_reply_handler;
  ResultCallback disable_technology_callback(
      Bind(&DisableTechnologyReplyHandler::ReportResult,
           disable_technology_reply_handler.AsWeakPtr()));
  EXPECT_CALL(disable_technology_reply_handler, ReportResult(_)).Times(0);

  manager()->SetEnabledStateForTechnology(kTypeEthernet, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsSuccess());

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kCellular));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kCellular));

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);

  // Ethernet Device is disabled, so disable succeeds immediately.
  EXPECT_CALL(*mock_devices_[0], SetEnabledPersistent(false, _, _))
      .WillOnce(WithArg<1>(Invoke(SetErrorSuccess)));
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeEthernet, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsSuccess());

  // Ethernet Device is enabled, and mock doesn't change error from
  // kOperationInitiated, so expect disable to say operation in progress.
  EXPECT_CALL(*mock_devices_[0], SetEnabledPersistent(false, _, _));
  mock_devices_[0]->enabled_ = true;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeEthernet, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsOngoing());

  // Ethernet Device is disabled, and mock doesn't change error from
  // kOperationInitiated, so expect enable to say operation in progress.
  EXPECT_CALL(*mock_devices_[0], SetEnabledPersistent(true, _, _));
  mock_devices_[0]->enabled_ = false;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeEthernet, true, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsOngoing());

  // Cellular Device is enabled, but disable failed.
  EXPECT_CALL(*mock_devices_[1], SetEnabledPersistent(false, _, _))
      .WillOnce(WithArg<1>(Invoke(SetErrorPermissionDenied)));
  mock_devices_[1]->enabled_ = true;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeCellular, false, true, &error,
                                          disable_technology_callback);
  EXPECT_EQ(Error::kPermissionDenied, error.type());

  // Multiple Cellular Devices in enabled state. Should indicate IsOngoing
  // if one is in progress (even if the other completed immediately).
  manager()->RegisterDevice(mock_devices_[2]);
  EXPECT_CALL(*mock_devices_[1], SetEnabledPersistent(false, _, _))
      .WillOnce(WithArg<1>(Invoke(SetErrorPermissionDenied)));
  EXPECT_CALL(*mock_devices_[2], SetEnabledPersistent(false, _, _));
  mock_devices_[1]->enabled_ = true;
  mock_devices_[2]->enabled_ = true;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeCellular, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsOngoing());

  // ...and order doesn't matter.
  EXPECT_CALL(*mock_devices_[1], SetEnabledPersistent(false, _, _));
  EXPECT_CALL(*mock_devices_[2], SetEnabledPersistent(false, _, _))
      .WillOnce(WithArg<1>(Invoke(SetErrorPermissionDenied)));
  mock_devices_[1]->enabled_ = true;
  mock_devices_[2]->enabled_ = true;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeCellular, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsOngoing());
  Mock::VerifyAndClearExpectations(&disable_technology_reply_handler);

  // Multiple Cellular Devices in enabled state. Even if all disable
  // operations complete asynchronously, we only get one call to the
  // DisableTechnologyReplyHandler::ReportResult.
  ResultCallback device1_result_callback;
  ResultCallback device2_result_callback;
  EXPECT_CALL(*mock_devices_[1], SetEnabledPersistent(false, _, _))
      .WillOnce(SaveArg<2>(&device1_result_callback));
  EXPECT_CALL(*mock_devices_[2], SetEnabledPersistent(false, _, _))
      .WillOnce(DoAll(WithArg<1>(Invoke(SetErrorPermissionDenied)),
                      SaveArg<2>(&device2_result_callback)));
  EXPECT_CALL(disable_technology_reply_handler, ReportResult(_));
  mock_devices_[1]->enabled_ = true;
  mock_devices_[2]->enabled_ = true;
  error.Populate(Error::kOperationInitiated);
  manager()->SetEnabledStateForTechnology(kTypeCellular, false, true, &error,
                                          disable_technology_callback);
  EXPECT_TRUE(error.IsOngoing());
  device1_result_callback.Run(Error(Error::kSuccess));
  device2_result_callback.Run(Error(Error::kSuccess));
}

TEST_F(ManagerTest, IgnoredSearchList) {
  std::unique_ptr<MockResolver> resolver(new StrictMock<MockResolver>());
  vector<string> ignored_paths;
  SetResolver(resolver.get());

  const string kIgnored0 = "chromium.org";
  ignored_paths.push_back(kIgnored0);
  EXPECT_CALL(*resolver, set_ignored_search_list(ignored_paths));
  SetIgnoredDNSSearchPaths(kIgnored0, nullptr);
  EXPECT_EQ(kIgnored0, GetIgnoredDNSSearchPaths());

  const string kIgnored1 = "google.com";
  const string kIgnoredSum = kIgnored0 + "," + kIgnored1;
  ignored_paths.push_back(kIgnored1);
  EXPECT_CALL(*resolver, set_ignored_search_list(ignored_paths));
  SetIgnoredDNSSearchPaths(kIgnoredSum, nullptr);
  EXPECT_EQ(kIgnoredSum, GetIgnoredDNSSearchPaths());

  ignored_paths.clear();
  EXPECT_CALL(*resolver, set_ignored_search_list(ignored_paths));
  SetIgnoredDNSSearchPaths("", nullptr);
  EXPECT_EQ("", GetIgnoredDNSSearchPaths());

  SetResolver(Resolver::GetInstance());
}

TEST_F(ManagerTest, PortalFallbackUrls) {
  const string kFallback0 = "http://fallback";
  const vector<string> kFallbackVec0 = {kFallback0};
  SetPortalFallbackUrlsString(kFallback0, nullptr);
  EXPECT_EQ(kFallbackVec0, GetPortalFallbackUrlsString());

  const string kFallback1 = "http://other";
  const string kFallbackSum = kFallback0 + "," + kFallback1;
  const vector<string> kFallbackVec1 = {kFallback0, kFallback1};
  SetPortalFallbackUrlsString(kFallbackSum, nullptr);
  EXPECT_EQ(kFallbackVec1, GetPortalFallbackUrlsString());

  SetPortalFallbackUrlsString("", nullptr);
  EXPECT_EQ(kFallbackVec1, GetPortalFallbackUrlsString());
}

TEST_F(ManagerTest, ServiceStateChangeEmitsServices) {
  // Test to make sure that every service state-change causes the
  // Manager to emit a new service list.
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));

  manager()->RegisterService(mock_service);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServiceCompleteListProperty, _))
      .Times(1);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServicesProperty, _))
      .Times(1);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServiceWatchListProperty, _))
      .Times(1);
  CompleteServiceSort();

  Mock::VerifyAndClearExpectations(manager_adaptor_);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServiceCompleteListProperty, _))
      .Times(1);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServicesProperty, _))
      .Times(1);
  EXPECT_CALL(*manager_adaptor_,
              EmitRpcIdentifierArrayChanged(kServiceWatchListProperty, _))
      .Times(1);
  manager()->UpdateService(mock_service.get());
  CompleteServiceSort();

  manager()->DeregisterService(mock_service);
}

TEST_F(ManagerTest, EnumerateServices) {
  MockServiceRefPtr mock_service(new NiceMock<MockService>(manager()));
  manager()->RegisterService(mock_service);

  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*mock_service, IsVisible()).WillRepeatedly(Return(false));
  EXPECT_TRUE(EnumerateAvailableServices().empty());
  EXPECT_TRUE(EnumerateWatchedServices().empty());

  EXPECT_CALL(*mock_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_TRUE(EnumerateAvailableServices().empty());
  EXPECT_TRUE(EnumerateWatchedServices().empty());

  EXPECT_CALL(*mock_service, IsVisible()).WillRepeatedly(Return(true));
  static const Service::ConnectState kUnwatchedStates[] = {
      Service::kStateUnknown, Service::kStateIdle, Service::kStateFailure};
  for (auto unwatched_state : kUnwatchedStates) {
    EXPECT_CALL(*mock_service, state()).WillRepeatedly(Return(unwatched_state));
    EXPECT_FALSE(EnumerateAvailableServices().empty());
    EXPECT_TRUE(EnumerateWatchedServices().empty());
  }

  static const Service::ConnectState kWatchedStates[] = {
      Service::kStateAssociating,   Service::kStateConfiguring,
      Service::kStateConnected,     Service::kStateNoConnectivity,
      Service::kStateRedirectFound, Service::kStateOnline};
  for (auto watched_state : kWatchedStates) {
    EXPECT_CALL(*mock_service, state()).WillRepeatedly(Return(watched_state));
    EXPECT_FALSE(EnumerateAvailableServices().empty());
    EXPECT_FALSE(EnumerateWatchedServices().empty());
  }

  manager()->DeregisterService(mock_service);
}

TEST_F(ManagerTest, ConnectToBestServices) {
  MockServiceRefPtr wifi_service0(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*wifi_service0, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*wifi_service0, IsConnected()).WillRepeatedly(Return(false));
  wifi_service0->SetConnectable(true);
  wifi_service0->SetAutoConnect(true);
  wifi_service0->SetSecurity(Service::kCryptoAes, true, true);
  EXPECT_CALL(*wifi_service0, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*wifi_service0, IsVisible()).WillRepeatedly(Return(false));
  EXPECT_CALL(*wifi_service0, explicitly_disconnected())
      .WillRepeatedly(Return(false));

  MockServiceRefPtr wifi_service1(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*wifi_service1, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*wifi_service1, IsVisible()).WillRepeatedly(Return(true));
  EXPECT_CALL(*wifi_service1, IsConnected()).WillRepeatedly(Return(false));
  wifi_service1->SetAutoConnect(true);
  wifi_service1->SetConnectable(true);
  wifi_service1->SetSecurity(Service::kCryptoRc4, true, true);
  EXPECT_CALL(*wifi_service1, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*wifi_service1, explicitly_disconnected())
      .WillRepeatedly(Return(false));

  MockServiceRefPtr wifi_service2(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*wifi_service2, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*wifi_service2, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*wifi_service2, IsVisible()).WillRepeatedly(Return(true));
  wifi_service2->SetAutoConnect(true);
  wifi_service2->SetConnectable(true);
  wifi_service2->SetSecurity(Service::kCryptoNone, false, false);
  EXPECT_CALL(*wifi_service2, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*wifi_service2, explicitly_disconnected())
      .WillRepeatedly(Return(false));

  manager()->RegisterService(wifi_service0);
  manager()->RegisterService(wifi_service1);
  manager()->RegisterService(wifi_service2);

  CompleteServiceSort();
  EXPECT_TRUE(ServiceOrderIs(wifi_service2, wifi_service0));

  MockServiceRefPtr cellular_service0(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*cellular_service0, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*cellular_service0, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*cellular_service0, IsVisible()).WillRepeatedly(Return(true));
  cellular_service0->SetAutoConnect(true);
  cellular_service0->SetConnectable(true);
  EXPECT_CALL(*cellular_service0, technology())
      .WillRepeatedly(Return(Technology::kCellular));
  EXPECT_CALL(*cellular_service0, explicitly_disconnected())
      .WillRepeatedly(Return(true));
  manager()->RegisterService(cellular_service0);

  MockServiceRefPtr cellular_service1(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*cellular_service1, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*cellular_service1, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*cellular_service1, IsVisible()).WillRepeatedly(Return(true));
  cellular_service1->SetAutoConnect(true);
  cellular_service1->SetConnectable(true);
  EXPECT_CALL(*cellular_service1, technology())
      .WillRepeatedly(Return(Technology::kCellular));
  EXPECT_CALL(*cellular_service1, explicitly_disconnected())
      .WillRepeatedly(Return(false));
  manager()->RegisterService(cellular_service1);

  MockServiceRefPtr vpn_service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*vpn_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*vpn_service, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*vpn_service, IsVisible()).WillRepeatedly(Return(true));
  vpn_service->SetAutoConnect(false);
  vpn_service->SetConnectable(true);
  EXPECT_CALL(*vpn_service, technology())
      .WillRepeatedly(Return(Technology::kVPN));
  manager()->RegisterService(vpn_service);

  // The connected services should be at the top.
  EXPECT_TRUE(ServiceOrderIs(wifi_service2, cellular_service1));

  EXPECT_CALL(*wifi_service0, Connect(_, _)).Times(0);  // Not visible.
  EXPECT_CALL(*wifi_service1, Connect(_, _));
  EXPECT_CALL(*wifi_service2, Connect(_, _)).Times(0);  // Lower prio.
  EXPECT_CALL(*cellular_service0, Connect(_, _))
      .Times(0);  // Explicitly disconnected.
  EXPECT_CALL(*cellular_service1, Connect(_, _)).Times(0);  // Is connected.
  EXPECT_CALL(*vpn_service, Connect(_, _)).Times(0);        // Not autoconnect.

  manager()->ConnectToBestServices(nullptr);
  dispatcher()->DispatchPendingEvents();

  // After this operation, since the Connect calls above are mocked and
  // no actual state changes have occurred, we should expect that the
  // service sorting order will not have changed.
  EXPECT_TRUE(ServiceOrderIs(wifi_service2, cellular_service1));
}

TEST_F(ManagerTest, CreateConnectivityReport) {
  // Add devices
  // WiFi
  auto wifi_device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "null", "addr", 0));
  manager()->RegisterDevice(wifi_device);
  // Cell
  auto cell_device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "null", "addr", 1));
  manager()->RegisterDevice(cell_device);
  // Ethernet
  auto eth_device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "null", "addr", 3));
  manager()->RegisterDevice(eth_device);
  // VPN Device -- base device for a service that will not be connected
  auto vpn_device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "null", "addr", 4));
  manager()->RegisterDevice(vpn_device);

  // Add service for multiple devices
  // WiFi
  MockServiceRefPtr wifi_service = new NiceMock<MockService>(manager());
  manager()->RegisterService(wifi_service);
  EXPECT_CALL(*wifi_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*wifi_service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*wifi_device, IsConnectedToService(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*wifi_device, IsConnectedToService(IsRefPtrTo(wifi_service)))
      .WillRepeatedly(Return(true));

  // Cell
  MockServiceRefPtr cell_service = new NiceMock<MockService>(manager());
  manager()->RegisterService(cell_service);
  EXPECT_CALL(*cell_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*cell_service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*cell_device, IsConnectedToService(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*cell_device, IsConnectedToService(IsRefPtrTo(cell_service)))
      .WillRepeatedly(Return(true));

  // Ethernet
  MockServiceRefPtr eth_service = new NiceMock<MockService>(manager());
  manager()->RegisterService(eth_service);
  EXPECT_CALL(*eth_service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*eth_service, IsConnected()).WillRepeatedly(Return(true));
  EXPECT_CALL(*eth_device, IsConnectedToService(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*eth_device, IsConnectedToService(IsRefPtrTo(eth_service)))
      .WillRepeatedly(Return(true));

  // VPN: Service exists but is not connected and will not trigger a
  // connectivity report.
  MockServiceRefPtr vpn_service = new NiceMock<MockService>(manager());
  manager()->RegisterService(vpn_service);
  EXPECT_CALL(*vpn_service, state())
      .WillRepeatedly(Return(Service::kStateIdle));
  EXPECT_CALL(*vpn_service, IsConnected()).WillRepeatedly(Return(false));

  EXPECT_CALL(*wifi_device, StartConnectivityTest()).WillOnce(Return(true));
  EXPECT_CALL(*cell_device, StartConnectivityTest()).WillOnce(Return(true));
  EXPECT_CALL(*eth_device, StartConnectivityTest()).WillOnce(Return(true));
  EXPECT_CALL(*vpn_device, StartConnectivityTest()).Times(0);
  manager()->CreateConnectivityReport(nullptr);
  dispatcher()->DispatchPendingEvents();
}

TEST_F(ManagerTest, IsProfileBefore) {
  scoped_refptr<MockProfile> profile0(new NiceMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(new NiceMock<MockProfile>(manager(), ""));

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);  // profile1 is after profile0.
  EXPECT_TRUE(manager()->IsProfileBefore(profile0, profile1));
  EXPECT_FALSE(manager()->IsProfileBefore(profile1, profile0));

  // A few abnormal cases, but it's good to track their behavior.
  scoped_refptr<MockProfile> profile2(new NiceMock<MockProfile>(manager(), ""));
  EXPECT_TRUE(manager()->IsProfileBefore(profile0, profile2));
  EXPECT_TRUE(manager()->IsProfileBefore(profile1, profile2));
  EXPECT_FALSE(manager()->IsProfileBefore(profile2, profile0));
  EXPECT_FALSE(manager()->IsProfileBefore(profile2, profile1));
}

TEST_F(ManagerTest, GetLoadableProfileEntriesForService) {
  MockStore storage0;
  MockStore storage1;
  MockStore storage2;

  scoped_refptr<MockProfile> profile0(new NiceMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile1(new NiceMock<MockProfile>(manager(), ""));
  scoped_refptr<MockProfile> profile2(new NiceMock<MockProfile>(manager(), ""));

  AdoptProfile(manager(), profile0);
  AdoptProfile(manager(), profile1);
  AdoptProfile(manager(), profile2);

  MockServiceRefPtr service(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*profile0, GetConstStorage()).WillOnce(Return(&storage0));
  EXPECT_CALL(*profile1, GetConstStorage()).WillOnce(Return(&storage1));
  EXPECT_CALL(*profile2, GetConstStorage()).WillOnce(Return(&storage2));

  const string kEntry0("aluminum_crutch");
  const string kEntry2("rehashed_faces");

  EXPECT_CALL(*service, GetLoadableStorageIdentifier(Ref(storage0)))
      .WillOnce(Return(kEntry0));
  EXPECT_CALL(*service, GetLoadableStorageIdentifier(Ref(storage1)))
      .WillOnce(Return(""));
  EXPECT_CALL(*service, GetLoadableStorageIdentifier(Ref(storage2)))
      .WillOnce(Return(kEntry2));

  const RpcIdentifier kProfileRpc0("service_station");
  const RpcIdentifier kProfileRpc2("crystal_tiaras");

  EXPECT_CALL(*profile0, GetRpcIdentifier()).WillOnce(Return(kProfileRpc0));
  EXPECT_CALL(*profile1, GetRpcIdentifier()).Times(0);
  EXPECT_CALL(*profile2, GetRpcIdentifier()).WillOnce(Return(kProfileRpc2));

  map<RpcIdentifier, string> entries =
      manager()->GetLoadableProfileEntriesForService(service);
  EXPECT_EQ(2, entries.size());
  EXPECT_TRUE(base::ContainsKey(entries, kProfileRpc0));
  EXPECT_TRUE(base::ContainsKey(entries, kProfileRpc2));
  EXPECT_EQ(kEntry0, entries[kProfileRpc0]);
  EXPECT_EQ(kEntry2, entries[kProfileRpc2]);
}

#if !defined(DISABLE_WIFI)
TEST_F(ManagerTest, InitializeProfilesInformsProviders) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  Manager manager(control_interface(), dispatcher(), metrics(), run_path(),
                  storage_path(), temp_dir.GetPath().value());
  // Can't use |wifi_provider_|, because it's owned by the Manager
  // object in the fixture.
  MockWiFiProvider* wifi_provider = new NiceMock<MockWiFiProvider>();
  manager.wifi_provider_.reset(wifi_provider);  // pass ownership
  manager.UpdateProviderMapping();
  // Give manager a valid place to write the user profile list.
  manager.user_profile_list_path_ =
      temp_dir.GetPath().Append("user_profile_list");

  // With no user profiles, the WiFiProvider should be called once
  // (for the default profile).
  EXPECT_CALL(*wifi_provider, CreateServicesFromProfile(_));
  manager.InitializeProfiles();
  Mock::VerifyAndClearExpectations(wifi_provider);

  // With |n| user profiles, the WiFiProvider should be called |n+1|
  // times. First, create 2 user profiles...
  const char kProfile0[] = "~user/profile0";
  const char kProfile1[] = "~user/profile1";
  RpcIdentifier profile_rpc_path;
  Error error;
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  manager.CreateProfile(kProfile0, &profile_rpc_path, &error);
  manager.PushProfile(kProfile0, &profile_rpc_path, &error);
  manager.CreateProfile(kProfile1, &profile_rpc_path, &error);
  manager.PushProfile(kProfile1, &profile_rpc_path, &error);

  // ... then reset manager state ...
  manager.profiles_.clear();

  // ...then check that the WiFiProvider is notified about all three
  // profiles (one default, two user).
  EXPECT_CALL(*wifi_provider, CreateServicesFromProfile(_)).Times(3);
  manager.InitializeProfiles();
  Mock::VerifyAndClearExpectations(wifi_provider);
}
#endif  // DISABLE_WIFI

TEST_F(ManagerTest, InitializeProfilesHandlesDefaults) {
  ScopedTempDir temp_dir;
  std::unique_ptr<Manager> manager;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Instantiate a Manager with empty persistent storage. Check that
  // defaults are set.
  //
  // Note that we use the same directory for default and user profiles.
  // This doesn't affect the test results, because we don't push a
  // user profile.
  manager.reset(new Manager(control_interface(), dispatcher(), metrics(),
                            run_path(), temp_dir.GetPath().value(),
                            temp_dir.GetPath().value()));
  manager->InitializeProfiles();
  EXPECT_EQ(PortalDetector::kDefaultCheckPortalList,
            manager->props_.check_portal_list);
  EXPECT_EQ(Resolver::kDefaultIgnoredSearchList,
            manager->props_.ignored_dns_search_paths);
  EXPECT_EQ(LinkMonitor::kDefaultLinkMonitorTechnologies,
            manager->props_.link_monitor_technologies);
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, manager->props_.portal_http_url);
  EXPECT_EQ(PortalDetector::kDefaultHttpsUrl, manager->props_.portal_https_url);
  EXPECT_EQ(PortalDetector::kDefaultFallbackHttpUrls,
            manager->props_.portal_fallback_http_urls);

  // Change one of the settings.
  static const string kCustomCheckPortalList = "fiber0";
  Error error;
  manager->SetCheckPortalList(kCustomCheckPortalList, &error);
  manager->profiles_[0]->Save();

  // Instantiate a new manager. It should have our settings for
  // check_portal_list, rather than the default.
  manager.reset(new Manager(control_interface(), dispatcher(), metrics(),
                            run_path(), temp_dir.GetPath().value(),
                            temp_dir.GetPath().value()));
  manager->InitializeProfiles();
  EXPECT_EQ(kCustomCheckPortalList, manager->props_.check_portal_list);

  // If we clear the persistent storage, we again get the default value.
  ASSERT_TRUE(temp_dir.Delete());
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  manager.reset(new Manager(control_interface(), dispatcher(), metrics(),
                            run_path(), temp_dir.GetPath().value(),
                            temp_dir.GetPath().value()));
  manager->InitializeProfiles();
  EXPECT_EQ(PortalDetector::kDefaultCheckPortalList,
            manager->props_.check_portal_list);
}

TEST_F(ManagerTest, ProfileStackChangeLogging) {
  ScopedTempDir temp_dir;
  std::unique_ptr<Manager> manager;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  manager.reset(new Manager(control_interface(), dispatcher(), metrics(),
                            run_path(), temp_dir.GetPath().value(),
                            temp_dir.GetPath().value()));

  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("1 profile(s)")));
  manager->InitializeProfiles();

  const char kProfile0[] = "~user/profile0";
  const char kProfile1[] = "~user/profile1";
  const char kProfile2[] = "~user/profile2";
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("user")));
  TestCreateProfile(manager.get(), kProfile0);
  TestCreateProfile(manager.get(), kProfile1);
  TestCreateProfile(manager.get(), kProfile2);

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("2 profile(s)")));
  TestPushProfile(manager.get(), kProfile0);

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("3 profile(s)")));
  TestInsertUserProfile(manager.get(), kProfile1, "not-so-random-string");

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("4 profile(s)")));
  TestInsertUserProfile(manager.get(), kProfile2, "very-random-string");

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("3 profile(s)")));
  TestPopProfile(manager.get(), kProfile2);

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("2 profile(s)")));
  TestPopAnyProfile(manager.get());

  EXPECT_CALL(log, Log(logging::LOG_INFO, _, HasSubstr("1 profile(s)")));
  TestPopAllUserProfiles(manager.get());
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(ManagerTest, CustomSetterNoopChange) {
  // SetCheckPortalList
  {
    static const string kCheckPortalList = "weird-device,weirder-device";
    Error error;
    // Set to known value.
    EXPECT_TRUE(SetCheckPortalList(kCheckPortalList, &error));
    EXPECT_TRUE(error.IsSuccess());
    // Set to same value.
    EXPECT_FALSE(SetCheckPortalList(kCheckPortalList, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // SetIgnoredDNSSearchPaths
  {
    NiceMock<MockResolver> resolver;
    static const string kIgnoredPaths = "example.com,example.org";
    Error error;
    SetResolver(&resolver);
    // Set to known value.
    EXPECT_CALL(resolver, set_ignored_search_list(_));
    EXPECT_TRUE(SetIgnoredDNSSearchPaths(kIgnoredPaths, &error));
    EXPECT_TRUE(error.IsSuccess());
    Mock::VerifyAndClearExpectations(&resolver);
    // Set to same value.
    EXPECT_CALL(resolver, set_ignored_search_list(_)).Times(0);
    EXPECT_FALSE(SetIgnoredDNSSearchPaths(kIgnoredPaths, &error));
    EXPECT_TRUE(error.IsSuccess());
    Mock::VerifyAndClearExpectations(&resolver);
  }
}

TEST_F(ManagerTest, GeoLocation) {
  EXPECT_TRUE(manager()->GetNetworksForGeolocation().empty());

  auto device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "device", "addr_1", 0));

  // Manager should ignore gelocation info from technologies it does not know.
  EXPECT_CALL(*device, technology())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(Technology::kEthernet));
  manager()->OnDeviceGeolocationInfoUpdated(device);
  EXPECT_TRUE(manager()->GetNetworksForGeolocation().empty());
  Mock::VerifyAndClearExpectations(device.get());

  // Manager should add WiFi geolocation info.
  EXPECT_CALL(*device, technology())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*device, GetGeolocationObjects())
      .WillOnce(Return(vector<GeolocationInfo>()));
  manager()->OnDeviceGeolocationInfoUpdated(device);
  auto location_infos = manager()->GetNetworksForGeolocation();
  EXPECT_EQ(1, location_infos.size());
  EXPECT_TRUE(base::ContainsKey(location_infos, kGeoWifiAccessPointsProperty));

  auto cellular_device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "modem", "addr_2", 1));

  // Manager should inclusively add cellular info.
  EXPECT_CALL(*cellular_device, technology())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(Technology::kCellular));
  EXPECT_CALL(*cellular_device, GetGeolocationObjects())
      .WillOnce(Return(vector<GeolocationInfo>()));
  manager()->OnDeviceGeolocationInfoUpdated(cellular_device);
  location_infos = manager()->GetNetworksForGeolocation();
  EXPECT_EQ(2, location_infos.size());
  EXPECT_TRUE(base::ContainsKey(location_infos, kGeoWifiAccessPointsProperty));
  EXPECT_TRUE(base::ContainsKey(location_infos, kGeoCellTowersProperty));
}

TEST_F(ManagerTest, GeoLocation_MultipleDevicesOneTechnology) {
  EXPECT_TRUE(manager()->GetNetworksForGeolocation().empty());

  auto device_1 = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "device_1", "addr_1", 0));
  GeolocationInfo info_1;
  info_1["location"] = "abc";

  auto device_2 = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "device_2", "addr_2", 1));
  GeolocationInfo info_2;
  info_2["location"] = "def";

  // Make both devices WiFi technology and have geolocation info.
  EXPECT_CALL(*device_1, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*device_1, GetGeolocationObjects())
      .WillOnce(Return(vector<GeolocationInfo>{info_1}));
  manager()->OnDeviceGeolocationInfoUpdated(device_1);

  EXPECT_CALL(*device_2, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*device_2, GetGeolocationObjects())
      .WillOnce(Return(vector<GeolocationInfo>{info_2}));
  manager()->OnDeviceGeolocationInfoUpdated(device_2);

  auto location_infos = manager()->GetNetworksForGeolocation();
  EXPECT_EQ(1, location_infos.size());
  EXPECT_TRUE(base::ContainsKey(location_infos, kGeoWifiAccessPointsProperty));

  // Check that both entries are in the list.
  EXPECT_EQ(2, location_infos[kGeoWifiAccessPointsProperty].size());
}

TEST_F(ManagerTest, GeoLocation_DeregisterDevice) {
  EXPECT_TRUE(manager()->GetNetworksForGeolocation().empty());

  auto device = make_scoped_refptr(
      new NiceMock<MockDevice>(manager(), "device", "addr_1", 0));
  manager()->RegisterDevice(device);

  EXPECT_CALL(*device, technology()).WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*device, GetGeolocationObjects())
      .WillOnce(Return(vector<GeolocationInfo>()));
  manager()->OnDeviceGeolocationInfoUpdated(device);

  auto location_infos = manager()->GetNetworksForGeolocation();
  EXPECT_EQ(1, location_infos.size());
  EXPECT_TRUE(base::ContainsKey(location_infos, kGeoWifiAccessPointsProperty));

  // When we deregister, the entries should go away.
  manager()->DeregisterDevice(device);
  location_infos = manager()->GetNetworksForGeolocation();
  EXPECT_EQ(0, location_infos.size());
}

TEST_F(ManagerTest, IsWifiIdle) {
  // No registered service.
  EXPECT_FALSE(manager()->IsWifiIdle());

  MockServiceRefPtr wifi_service(new MockService(manager()));
  MockServiceRefPtr cell_service(new MockService(manager()));

  manager()->RegisterService(wifi_service);
  manager()->RegisterService(cell_service);

  EXPECT_CALL(*wifi_service, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*cell_service, technology())
      .WillRepeatedly(Return(Technology::kCellular));

  // Cellular is connected.
  EXPECT_CALL(*cell_service, IsConnected()).WillRepeatedly(Return(true));
  manager()->UpdateService(cell_service);

  // No wifi connection attempt.
  EXPECT_CALL(*wifi_service, IsConnecting()).WillRepeatedly(Return(false));
  EXPECT_CALL(*wifi_service, IsConnected()).WillRepeatedly(Return(false));
  manager()->UpdateService(wifi_service);
  EXPECT_TRUE(manager()->IsWifiIdle());

  // Attempt wifi connection.
  Mock::VerifyAndClearExpectations(wifi_service.get());
  EXPECT_CALL(*wifi_service, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*wifi_service, IsConnecting()).WillRepeatedly(Return(true));
  EXPECT_CALL(*wifi_service, IsConnected()).WillRepeatedly(Return(false));
  manager()->UpdateService(wifi_service);
  EXPECT_FALSE(manager()->IsWifiIdle());

  // wifi connected.
  Mock::VerifyAndClearExpectations(wifi_service.get());
  EXPECT_CALL(*wifi_service, technology())
      .WillRepeatedly(Return(Technology::kWifi));
  EXPECT_CALL(*wifi_service, IsConnecting()).WillRepeatedly(Return(false));
  EXPECT_CALL(*wifi_service, IsConnected()).WillRepeatedly(Return(true));
  manager()->UpdateService(wifi_service);
  EXPECT_FALSE(manager()->IsWifiIdle());
}

TEST_F(ManagerTest, DetectMultiHomedDevices) {
  vector<scoped_refptr<MockConnection>> mock_connections;
  vector<ConnectionRefPtr> device_connections;
  mock_devices_.push_back(
      new NiceMock<MockDevice>(manager(), "null4", "addr4", 0));
  mock_devices_.push_back(
      new NiceMock<MockDevice>(manager(), "null5", "addr5", 0));
  for (const auto& device : mock_devices_) {
    manager()->RegisterDevice(device);
    mock_connections.emplace_back(
        new NiceMock<MockConnection>(device_info_.get()));
    device_connections.emplace_back(mock_connections.back());
  }
  EXPECT_CALL(*mock_connections[1], GetSubnetName()).WillOnce(Return("1"));
  EXPECT_CALL(*mock_connections[2], GetSubnetName()).WillOnce(Return("2"));
  EXPECT_CALL(*mock_connections[3], GetSubnetName()).WillOnce(Return("1"));
  EXPECT_CALL(*mock_connections[4], GetSubnetName()).WillOnce(Return(""));
  EXPECT_CALL(*mock_connections[5], GetSubnetName()).WillOnce(Return(""));

  // Do not assign a connection to mock_devices_[0].
  EXPECT_CALL(*mock_devices_[1], connection())
      .WillRepeatedly(ReturnRef(device_connections[1]));
  EXPECT_CALL(*mock_devices_[2], connection())
      .WillRepeatedly(ReturnRef(device_connections[2]));
  EXPECT_CALL(*mock_devices_[3], connection())
      .WillRepeatedly(ReturnRef(device_connections[3]));
  EXPECT_CALL(*mock_devices_[4], connection())
      .WillRepeatedly(ReturnRef(device_connections[4]));
  EXPECT_CALL(*mock_devices_[5], connection())
      .WillRepeatedly(ReturnRef(device_connections[5]));

  EXPECT_CALL(*mock_devices_[0], SetIsMultiHomed(false));
  EXPECT_CALL(*mock_devices_[1], SetIsMultiHomed(true));
  EXPECT_CALL(*mock_devices_[2], SetIsMultiHomed(false));
  EXPECT_CALL(*mock_devices_[3], SetIsMultiHomed(true));
  EXPECT_CALL(*mock_devices_[4], SetIsMultiHomed(false));
  EXPECT_CALL(*mock_devices_[5], SetIsMultiHomed(false));
  manager()->DetectMultiHomedDevices();
}

TEST_F(ManagerTest, IsTechnologyProhibited) {
  // Test initial state.
  EXPECT_EQ("", manager()->props_.prohibited_technologies);
  EXPECT_FALSE(manager()->IsTechnologyProhibited(Technology::kCellular));
  EXPECT_FALSE(manager()->IsTechnologyProhibited(Technology::kVPN));

  Error smoke_error;
  EXPECT_FALSE(
      manager()->SetProhibitedTechnologies("smoke_signal", &smoke_error));
  EXPECT_EQ(Error::kInvalidArguments, smoke_error.type());

  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kVPN));
  ON_CALL(*mock_devices_[1], technology())
      .WillByDefault(Return(Technology::kCellular));
  ON_CALL(*mock_devices_[2], technology())
      .WillByDefault(Return(Technology::kWifi));

  manager()->RegisterDevice(mock_devices_[0]);
  manager()->RegisterDevice(mock_devices_[1]);
  manager()->RegisterDevice(mock_devices_[2]);

  // Registered devices of prohibited technology types should be disabled.
  EXPECT_CALL(*mock_devices_[0], SetEnabledNonPersistent(false, _, _));
  EXPECT_CALL(*mock_devices_[1], SetEnabledNonPersistent(false, _, _));
  EXPECT_CALL(*mock_devices_[2], SetEnabledNonPersistent(false, _, _)).Times(0);
  Error error;
  manager()->SetProhibitedTechnologies("cellular,vpn", &error);
  EXPECT_TRUE(manager()->IsTechnologyProhibited(Technology::kVPN));
  EXPECT_TRUE(manager()->IsTechnologyProhibited(Technology::kCellular));
  EXPECT_FALSE(manager()->IsTechnologyProhibited(Technology::kWifi));
  Mock::VerifyAndClearExpectations(mock_devices_[0].get());
  Mock::VerifyAndClearExpectations(mock_devices_[1].get());
  Mock::VerifyAndClearExpectations(mock_devices_[2].get());

  // Newly registered devices should be disabled.
  mock_devices_.push_back(
      new NiceMock<MockDevice>(manager(), "null4", "addr4", 0));
  mock_devices_.push_back(
      new NiceMock<MockDevice>(manager(), "null5", "addr5", 0));
  ON_CALL(*mock_devices_[3], technology())
      .WillByDefault(Return(Technology::kVPN));
  ON_CALL(*mock_devices_[4], technology())
      .WillByDefault(Return(Technology::kCellular));
  ON_CALL(*mock_devices_[5], technology())
      .WillByDefault(Return(Technology::kWifi));

  EXPECT_CALL(*mock_devices_[3], SetEnabledNonPersistent(false, _, _));
  EXPECT_CALL(*mock_devices_[4], SetEnabledNonPersistent(false, _, _));
  EXPECT_CALL(*mock_devices_[5], SetEnabledPersistent(false, _, _)).Times(0);

  manager()->RegisterDevice(mock_devices_[3]);
  manager()->RegisterDevice(mock_devices_[4]);
  manager()->RegisterDevice(mock_devices_[5]);
  Mock::VerifyAndClearExpectations(mock_devices_[3].get());
  Mock::VerifyAndClearExpectations(mock_devices_[4].get());
  Mock::VerifyAndClearExpectations(mock_devices_[5].get());

  // Calls to enable a non-prohibited technology should succeed.
  Error enable_error(Error::kOperationInitiated);
  DisableTechnologyReplyHandler technology_reply_handler;
  ResultCallback enable_technology_callback(
      Bind(&DisableTechnologyReplyHandler::ReportResult,
           technology_reply_handler.AsWeakPtr()));
  EXPECT_CALL(*mock_devices_[2], SetEnabledPersistent(true, _, _));
  EXPECT_CALL(*mock_devices_[5], SetEnabledPersistent(true, _, _));
  manager()->SetEnabledStateForTechnology("wifi", true, true, &enable_error,
                                          enable_technology_callback);
  EXPECT_EQ(Error::kOperationInitiated, enable_error.type());

  // Calls to enable a prohibited technology should fail.
  Error enable_prohibited_error(Error::kOperationInitiated);
  EXPECT_CALL(*mock_devices_[0], SetEnabledPersistent(true, _, _)).Times(0);
  EXPECT_CALL(*mock_devices_[3], SetEnabledPersistent(true, _, _)).Times(0);
  manager()->SetEnabledStateForTechnology(
      "vpn", true, true, &enable_prohibited_error, enable_technology_callback);
  EXPECT_EQ(Error::kPermissionDenied, enable_prohibited_error.type());
}

TEST_F(ManagerTest, ClaimBlacklistedDevice) {
  const string kClaimerName = "test_claimer";
  const string kDeviceName = "test_device";

  // Set blacklisted devices.
  vector<string> blacklisted_devices = {kDeviceName};
  manager()->SetBlacklistedDevices(blacklisted_devices);

  Error error;
  manager()->ClaimDevice(kClaimerName, kDeviceName, &error);
  EXPECT_TRUE(error.IsFailure());
  EXPECT_EQ("Not allowed to claim unmanaged device", error.message());
  // Verify device claimer is not created.
  EXPECT_EQ(nullptr, manager()->device_claimer_);
}

TEST_F(ManagerTest, ReleaseBlacklistedDevice) {
  const string kClaimerName = "test_claimer";
  const string kDeviceName = "test_device";

  // Set blacklisted devices.
  vector<string> blacklisted_devices = {kDeviceName};
  manager()->SetBlacklistedDevices(blacklisted_devices);

  Error error;
  bool claimer_removed;
  manager()->ReleaseDevice(kClaimerName, kDeviceName, &claimer_removed, &error);
  EXPECT_TRUE(error.IsFailure());
  EXPECT_FALSE(claimer_removed);
  EXPECT_EQ("Not allowed to release unmanaged device", error.message());
}

TEST_F(ManagerTest, BlacklistedDeviceIsNotManaged) {
  const string kDeviceName = "test_device";

  vector<string> blacklisted_devices = {kDeviceName};
  manager()->SetBlacklistedDevices(blacklisted_devices);
  EXPECT_FALSE(manager()->DeviceManagementAllowed(kDeviceName));
}

TEST_F(ManagerTest, NonBlacklistedDeviceIsManaged) {
  const string kDeviceName = "test_device";

  vector<string> blacklisted_devices = {"other_device"};
  manager()->SetBlacklistedDevices(blacklisted_devices);
  EXPECT_TRUE(manager()->DeviceManagementAllowed(kDeviceName));
}

TEST_F(ManagerTest, WhitelistedDeviceIsManaged) {
  const string kDeviceName = "test_device";

  vector<string> whitelisted_devices = {kDeviceName};
  manager()->SetWhitelistedDevices(whitelisted_devices);
  EXPECT_TRUE(manager()->DeviceManagementAllowed(kDeviceName));
}

TEST_F(ManagerTest, NonWhitelistedDeviceIsNotManaged) {
  const string kDeviceName = "test_device";

  vector<string> whitelisted_devices = {"other_device"};
  manager()->SetWhitelistedDevices(whitelisted_devices);
  EXPECT_FALSE(manager()->DeviceManagementAllowed(kDeviceName));
}

TEST_F(ManagerTest, DevicesIsManagedByDefault) {
  EXPECT_TRUE(manager()->DeviceManagementAllowed("test_device"));
}

TEST_F(ManagerTest, ClaimDeviceWithoutClaimer) {
  const char kClaimerName[] = "test_claimer1";
  const char kDeviceName[] = "test_device";

  // Claim device when device claimer doesn't exist yet.
  Error error;
  manager()->ClaimDevice(kClaimerName, kDeviceName, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_TRUE(manager()->device_info()->IsDeviceBlackListed(kDeviceName));
  // Verify device claimer is created.
  EXPECT_NE(nullptr, manager()->device_claimer_);
}

TEST_F(ManagerTest, ClaimDeviceWithClaimer) {
  const char kClaimer1Name[] = "test_claimer1";
  const char kClaimer2Name[] = "test_claimer2";
  const char kDeviceName[] = "test_device";

  // Claim device with empty string name.
  const char kEmptyDeviceNameError[] = "Empty device name";
  Error error;
  manager()->ClaimDevice(kClaimer1Name, "", &error);
  EXPECT_EQ(string(kEmptyDeviceNameError), error.message());

  // Device claim succeed.
  error.Reset();
  manager()->ClaimDevice(kClaimer1Name, kDeviceName, &error);
  EXPECT_TRUE(error.IsSuccess());

  // Claimer mismatch, current implementation only allows one claimer at a time.
  const char kInvalidClaimerError[] =
      "Invalid claimer name test_claimer2. Claimer test_claimer1 already exist";
  error.Reset();
  manager()->ClaimDevice(kClaimer2Name, kDeviceName, &error);
  EXPECT_TRUE(error.IsFailure());
  EXPECT_EQ(string(kInvalidClaimerError), error.message());
}

TEST_F(ManagerTest, ClaimRegisteredDevice) {
  // Register a device to manager.
  ON_CALL(*mock_devices_[0], technology())
      .WillByDefault(Return(Technology::kWifi));
  manager()->RegisterDevice(mock_devices_[0]);
  // Verify device is registered.
  EXPECT_TRUE(IsDeviceRegistered(mock_devices_[0], Technology::kWifi));

  // Claim the registered device.
  Error error;
  manager()->ClaimDevice("claimer1", mock_devices_[0]->link_name(), &error);
  EXPECT_TRUE(error.IsSuccess());

  // Expect device to not be registered anymore.
  EXPECT_FALSE(IsDeviceRegistered(mock_devices_[0], Technology::kWifi));
}

TEST_F(ManagerTest, ReleaseDeviceWithoutClaimer) {
  bool claimer_removed;
  Error error;
  manager()->ReleaseDevice("claimer1", "device1", &claimer_removed, &error);
  EXPECT_FALSE(claimer_removed);
  EXPECT_THAT(
      error, ErrorIs(Error::kInvalidArguments, "Device claimer doesn't exist"));
}

TEST_F(ManagerTest, ReleaseDeviceFromWrongClaimer) {
  const char kDeviceName[] = "device1";

  Error error;
  manager()->ClaimDevice("claimer1", kDeviceName, &error);
  EXPECT_TRUE(error.IsSuccess());

  bool claimer_removed;
  manager()->ReleaseDevice("claimer2", kDeviceName, &claimer_removed, &error);
  EXPECT_FALSE(claimer_removed);
  EXPECT_THAT(
      error,
      ErrorIs(Error::kInvalidArguments,
              "Invalid claimer name claimer2. Claimer claimer1 already exist"));
}

TEST_F(ManagerTest, ReleaseDeviceFromDefaultClaimer) {
  const char kDeviceName[] = "device1";

  manager()->SetPassiveMode();
  VerifyPassiveMode();

  Error error;
  manager()->ClaimDevice("", kDeviceName, &error);
  EXPECT_TRUE(error.IsSuccess());

  // Release a device with default claimer. Claimer should not be resetted.
  bool claimer_removed;
  manager()->ReleaseDevice("", kDeviceName, &claimer_removed, &error);
  EXPECT_FALSE(claimer_removed);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ManagerTest, ReleaseDeviceFromNonDefaultClaimer) {
  const char kClaimerName[] = "claimer1";
  const char kDevice1Name[] = "device1";
  const char kDevice2Name[] = "device2";

  Error error;
  manager()->ClaimDevice(kClaimerName, kDevice1Name, &error);
  EXPECT_TRUE(error.IsSuccess());
  manager()->ClaimDevice(kClaimerName, kDevice2Name, &error);
  EXPECT_TRUE(error.IsSuccess());

  bool claimer_removed;
  manager()->ReleaseDevice(kClaimerName, kDevice1Name, &claimer_removed,
                           &error);
  EXPECT_FALSE(claimer_removed);
  EXPECT_TRUE(error.IsSuccess());

  // Release last device with non-default claimer. Claimer should be resetted.
  manager()->ReleaseDevice(kClaimerName, kDevice2Name, &claimer_removed,
                           &error);
  EXPECT_TRUE(claimer_removed);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(ManagerTest, GetEnabledDeviceWithTechnology) {
  auto ethernet_device = mock_devices_[0];
  auto wifi_device = mock_devices_[1];
  auto cellular_device = mock_devices_[2];
  ON_CALL(*ethernet_device, technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*wifi_device, technology()).WillByDefault(Return(Technology::kWifi));
  ON_CALL(*cellular_device, technology())
      .WillByDefault(Return(Technology::kCellular));
  ethernet_device->enabled_ = true;
  wifi_device->enabled_ = true;
  cellular_device->enabled_ = true;

  manager()->RegisterDevice(ethernet_device);
  manager()->RegisterDevice(wifi_device);
  manager()->RegisterDevice(cellular_device);

  EXPECT_EQ(ethernet_device,
            manager()->GetEnabledDeviceWithTechnology(Technology::kEthernet));
  EXPECT_EQ(wifi_device,
            manager()->GetEnabledDeviceWithTechnology(Technology::kWifi));
  EXPECT_EQ(cellular_device,
            manager()->GetEnabledDeviceWithTechnology(Technology::kCellular));
}

TEST_F(ManagerTest, GetEnabledDeviceByLinkName) {
  auto ethernet_device = mock_devices_[0];
  auto wifi_device = mock_devices_[1];
  auto disabled_wifi_device = mock_devices_[2];
  ON_CALL(*ethernet_device, technology())
      .WillByDefault(Return(Technology::kEthernet));
  ON_CALL(*wifi_device, technology()).WillByDefault(Return(Technology::kWifi));
  ON_CALL(*disabled_wifi_device, technology())
      .WillByDefault(Return(Technology::kWifi));
  ethernet_device->enabled_ = true;
  wifi_device->enabled_ = true;
  disabled_wifi_device->enabled_ = false;

  manager()->RegisterDevice(ethernet_device);
  manager()->RegisterDevice(wifi_device);

  EXPECT_EQ(ethernet_device, manager()->GetEnabledDeviceByLinkName(
                                 ethernet_device->link_name()));
  EXPECT_EQ(wifi_device,
            manager()->GetEnabledDeviceByLinkName(wifi_device->link_name()));
  EXPECT_EQ(nullptr, manager()->GetEnabledDeviceByLinkName(
                         disabled_wifi_device->link_name()));
}

TEST_F(ManagerTest, AcceptHostnameFrom) {
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("wlan0"));

  manager()->SetAcceptHostnameFrom("eth0");
  EXPECT_TRUE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("wlan0"));

  manager()->SetAcceptHostnameFrom("eth1");
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_TRUE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("wlan0"));

  manager()->SetAcceptHostnameFrom("eth*");
  EXPECT_TRUE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_TRUE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("wlan0"));

  manager()->SetAcceptHostnameFrom("wlan*");
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_TRUE(manager()->ShouldAcceptHostnameFrom("wlan0"));

  manager()->SetAcceptHostnameFrom("ether*");
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth0"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("eth1"));
  EXPECT_FALSE(manager()->ShouldAcceptHostnameFrom("wlan0"));
}

TEST_F(ManagerTest, DHCPv6EnabledDevices) {
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("eth0"));
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("eth1"));
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("wlan0"));

  vector<string> enabled_devices;
  enabled_devices.push_back("eth0");
  manager()->SetDHCPv6EnabledDevices(enabled_devices);
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("eth0"));
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("eth1"));
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("wlan0"));

  enabled_devices.push_back("eth1");
  manager()->SetDHCPv6EnabledDevices(enabled_devices);
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("eth0"));
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("eth1"));
  EXPECT_FALSE(manager()->IsDHCPv6EnabledForDevice("wlan0"));

  enabled_devices.push_back("wlan0");
  manager()->SetDHCPv6EnabledDevices(enabled_devices);
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("eth0"));
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("eth1"));
  EXPECT_TRUE(manager()->IsDHCPv6EnabledForDevice("wlan0"));
}

TEST_F(ManagerTest, FilterPrependDNSServersByFamily) {
  const struct {
    IPAddress::Family family;
    string prepend_value;
    vector<string> output_list;
  } expectations[] = {
      {IPAddress::kFamilyIPv4, "", {}},
      {IPAddress::kFamilyIPv4, "8.8.8.8", {"8.8.8.8"}},
      {IPAddress::kFamilyIPv4, "8.8.8.8,2001:4860:4860::8888", {"8.8.8.8"}},
      {IPAddress::kFamilyIPv4, "2001:4860:4860::8844", {}},
      {IPAddress::kFamilyIPv6, "", {}},
      {IPAddress::kFamilyIPv6, "8.8.8.8", {}},
      {IPAddress::kFamilyIPv6,
       "2001:4860:4860::8844",
       {"2001:4860:4860::8844"}},
      {IPAddress::kFamilyIPv6,
       "8.8.8.8,2001:4860:4860::8888",
       {"2001:4860:4860::8888"}}};

  for (const auto& expectation : expectations) {
    manager()->SetPrependDNSServers(expectation.prepend_value);
    auto dns_servers =
        manager()->FilterPrependDNSServersByFamily(expectation.family);
    EXPECT_EQ(expectation.output_list, dns_servers);
  }
}

TEST_F(ManagerTest, SetAlwaysOnVpnPackage) {
  const string kPackage = "com.example.test.vpn";
  EXPECT_EQ("", manager()->GetAlwaysOnVpnPackage(nullptr));

  // If the package is not changed, return false
  EXPECT_EQ(false, manager()->SetAlwaysOnVpnPackage("", nullptr));
  EXPECT_EQ("", manager()->GetAlwaysOnVpnPackage(nullptr));

  // If the package is not changed, return true
  EXPECT_EQ(true, manager()->SetAlwaysOnVpnPackage(kPackage, nullptr));
  EXPECT_EQ(kPackage, manager()->GetAlwaysOnVpnPackage(nullptr));

  EXPECT_EQ(false, manager()->SetAlwaysOnVpnPackage(kPackage, nullptr));
  EXPECT_EQ(kPackage, manager()->GetAlwaysOnVpnPackage(nullptr));

  EXPECT_EQ(true, manager()->SetAlwaysOnVpnPackage("", nullptr));
  EXPECT_EQ("", manager()->GetAlwaysOnVpnPackage(nullptr));
}

TEST_F(ManagerTest, ShouldBlackholeUserTraffic) {
  const string kRegistered = mock_devices_[0]->UniqueName();
  const string kUnregistered = mock_devices_[1]->UniqueName();

  manager()->RegisterDevice(mock_devices_[0]);

  const string kOnlinePackage = "com.example.test.vpn1";
  const string kOfflinePackage = "com.example.test.vpn2";
  const string kOtherPackage = "com.example.test.vpn3";

  MockServiceRefPtr online_service(new NiceMock<MockService>(manager()));
  MockServiceRefPtr offline_service(new NiceMock<MockService>(manager()));

  EXPECT_CALL(*online_service, IsOnline()).WillRepeatedly(Return(false));
  EXPECT_CALL(*online_service, IsAlwaysOnVpn(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*online_service, IsAlwaysOnVpn(kOnlinePackage))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*offline_service, IsOnline()).WillRepeatedly(Return(false));
  EXPECT_CALL(*offline_service, IsAlwaysOnVpn(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*offline_service, IsAlwaysOnVpn(kOfflinePackage))
      .WillRepeatedly(Return(true));
  manager()->RegisterService(online_service);
  manager()->RegisterService(offline_service);

  // No package set: no blackholing
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kRegistered));
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kUnregistered));

  // Package set, service is not online yet, blackhole all registered devices
  manager()->SetAlwaysOnVpnPackage(kOnlinePackage, nullptr);
  EXPECT_EQ(true, manager()->ShouldBlackholeUserTraffic(kRegistered));
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kUnregistered));

  // Service comes online, stop blackholing
  EXPECT_CALL(*online_service, IsOnline()).WillRepeatedly(Return(true));
  manager()->UpdateBlackholeUserTraffic();
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kRegistered));
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kUnregistered));

  // Set to a different package whose service is offline, resume blackholing
  manager()->SetAlwaysOnVpnPackage(kOfflinePackage, nullptr);
  EXPECT_EQ(true, manager()->ShouldBlackholeUserTraffic(kRegistered));
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kUnregistered));

  // Set to a different package which has no service, keep blackholing
  manager()->SetAlwaysOnVpnPackage(kOtherPackage, nullptr);
  EXPECT_EQ(true, manager()->ShouldBlackholeUserTraffic(kRegistered));
  EXPECT_EQ(false, manager()->ShouldBlackholeUserTraffic(kUnregistered));
}

TEST_F(ManagerTest, UpdateBlackholeUserTraffic) {
  manager()->RegisterDevice(mock_devices_[0]);

  const string kOnlinePackage = "com.example.test.vpn1";
  const string kOtherPackage = "com.example.test.vpn2";

  MockServiceRefPtr service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*service, IsOnline()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service, IsAlwaysOnVpn(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*service, IsAlwaysOnVpn(kOnlinePackage))
      .WillRepeatedly(Return(true));
  manager()->RegisterService(service);

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(1);
  manager()->SetAlwaysOnVpnPackage(kOtherPackage, nullptr);

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(0);
  manager()->SetAlwaysOnVpnPackage(kOnlinePackage, nullptr);

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(0);
  manager()->UpdateBlackholeUserTraffic();

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(1);
  EXPECT_CALL(*service, IsOnline()).WillRepeatedly(Return(true));
  manager()->UpdateBlackholeUserTraffic();

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(1);
  manager()->SetAlwaysOnVpnPackage(kOtherPackage, nullptr);

  EXPECT_CALL(*mock_devices_[0], UpdateBlackholeUserTraffic()).Times(1);
  manager()->SetAlwaysOnVpnPackage("", nullptr);
}

}  // namespace shill
