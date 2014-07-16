// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <ctype.h>
#include <sys/socket.h>
#include <linux/if.h>  // Needs typedefs from sys/socket.h.

#include <map>
#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/bind.h>
#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus-c++/dbus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/dbus_adaptor.h"
#include "shill/dhcp_provider.h"
#include "shill/event_dispatcher.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_connection_health_checker.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_dhcp_config.h"
#include "shill/mock_dhcp_provider.h"
#include "shill/mock_dns_server_tester.h"
#include "shill/mock_glib.h"
#include "shill/mock_ip_address_store.h"
#include "shill/mock_ipconfig.h"
#include "shill/mock_link_monitor.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_portal_detector.h"
#include "shill/mock_rtnl_handler.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
#include "shill/mock_traffic_monitor.h"
#include "shill/portal_detector.h"
#include "shill/property_store_unittest.h"
#include "shill/static_ip_parameters.h"
#include "shill/technology.h"
#include "shill/testing.h"
#include "shill/tethering.h"
#include "shill/traffic_monitor.h"

using base::Bind;
using base::Callback;
using std::map;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DefaultValue;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Values;

namespace shill {

class TestDevice : public Device {
 public:
  TestDevice(ControlInterface *control_interface,
             EventDispatcher *dispatcher,
             Metrics *metrics,
             Manager *manager,
             const std::string &link_name,
             const std::string &address,
             int interface_index,
             Technology::Identifier technology)
      : Device(control_interface, dispatcher, metrics, manager, link_name,
               address, interface_index, technology) {
    ON_CALL(*this, IsIPv6Allowed())
        .WillByDefault(Invoke(this, &TestDevice::DeviceIsIPv6Allowed));
    ON_CALL(*this, SetIPFlag(_, _, _))
        .WillByDefault(Invoke(this, &TestDevice::DeviceSetIPFlag));
    ON_CALL(*this, IsTrafficMonitorEnabled())
        .WillByDefault(Invoke(this,
                              &TestDevice::DeviceIsTrafficMonitorEnabled));
  }

  ~TestDevice() {}

  virtual void Start(Error *error,
                     const EnabledStateChangedCallback &callback) {
    DCHECK(error);
  }

  virtual void Stop(Error *error,
                    const EnabledStateChangedCallback &callback) {
    DCHECK(error);
  }

  MOCK_CONST_METHOD0(IsIPv6Allowed, bool());
  MOCK_CONST_METHOD0(IsTrafficMonitorEnabled, bool());

  MOCK_METHOD3(SetIPFlag, bool(IPAddress::Family family,
                               const std::string &flag,
                               const std::string &value));

  virtual bool DeviceIsIPv6Allowed() const {
    return Device::IsIPv6Allowed();
  }

  virtual bool DeviceIsTrafficMonitorEnabled() const {
    return Device::IsTrafficMonitorEnabled();
  }

  virtual bool DeviceSetIPFlag(IPAddress::Family family,
                               const std::string &flag,
                               const std::string &value) {
    return Device::SetIPFlag(family, flag, value);
  }
};

class DeviceTest : public PropertyStoreTest {
 public:
  DeviceTest()
      : device_(new TestDevice(control_interface(),
                               dispatcher(),
                               NULL,
                               manager(),
                               kDeviceName,
                               kDeviceAddress,
                               kDeviceInterfaceIndex,
                               Technology::kUnknown)),
        device_info_(control_interface(), NULL, NULL, NULL),
        metrics_(dispatcher()) {
    DHCPProvider::GetInstance()->glib_ = glib();
    DHCPProvider::GetInstance()->control_interface_ = control_interface();
    DHCPProvider::GetInstance()->dispatcher_ = dispatcher();
  }
  virtual ~DeviceTest() {}

  virtual void SetUp() {
    device_->metrics_ = &metrics_;
    device_->rtnl_handler_ = &rtnl_handler_;
  }

 protected:
  static const char kDeviceName[];
  static const char kDeviceAddress[];
  static const int kDeviceInterfaceIndex;

  void OnIPConfigUpdated(const IPConfigRefPtr &ipconfig) {
    device_->OnIPConfigUpdated(ipconfig);
  }

  void OnIPConfigFailed(const IPConfigRefPtr &ipconfig) {
    device_->OnIPConfigFailed(ipconfig);
  }

  void OnIPConfigExpired(const IPConfigRefPtr &ipconfig) {
    device_->OnIPConfigExpired(ipconfig);
  }

  void SelectService(const ServiceRefPtr service) {
    device_->SelectService(service);
  }

  void SetConnection(ConnectionRefPtr connection) {
    device_->connection_ = connection;
  }

  void SetLinkMonitor(LinkMonitor *link_monitor) {
    device_->set_link_monitor(link_monitor);  // Passes ownership.
  }

  bool HasLinkMonitor() {
    return device_->link_monitor();
  }

  bool StartLinkMonitor() {
    return device_->StartLinkMonitor();
  }

  void StopLinkMonitor() {
    device_->StopLinkMonitor();
  }

  uint64 GetLinkMonitorResponseTime(Error *error) {
    return device_->GetLinkMonitorResponseTime(error);
  }

  void SetTrafficMonitor(TrafficMonitor *traffic_monitor) {
    device_->set_traffic_monitor(traffic_monitor);  // Passes ownership.
  }

  void StartTrafficMonitor() {
    device_->StartTrafficMonitor();
  }

  void StopTrafficMonitor() {
    device_->StopTrafficMonitor();
  }

  void NetworkProblemDetected(int reason) {
    device_->OnEncounterNetworkProblem(reason);
  }

  DeviceMockAdaptor *GetDeviceMockAdaptor() {
    return dynamic_cast<DeviceMockAdaptor *>(device_->adaptor_.get());
  }

  void SetManager(Manager *manager) {
    device_->manager_ = manager;
  }

  MockControl control_interface_;
  scoped_refptr<TestDevice> device_;
  MockDeviceInfo device_info_;
  MockMetrics metrics_;
  StrictMock<MockRTNLHandler> rtnl_handler_;
};

const char DeviceTest::kDeviceName[] = "testdevice";
const char DeviceTest::kDeviceAddress[] = "address";
const int DeviceTest::kDeviceInterfaceIndex = 0;

TEST_F(DeviceTest, Contains) {
  EXPECT_TRUE(device_->store().Contains(kNameProperty));
  EXPECT_FALSE(device_->store().Contains(""));
}

TEST_F(DeviceTest, GetProperties) {
  map<string, ::DBus::Variant> props;
  Error error(Error::kInvalidProperty, "");
  ::DBus::Error dbus_error;
  DBusAdaptor::GetProperties(device_->store(), &props, &dbus_error);
  ASSERT_FALSE(props.find(kNameProperty) == props.end());
  EXPECT_EQ(props[kNameProperty].reader().get_string(), string(kDeviceName));
}

// Note: there are currently no writeable Device properties that
// aren't registered in a subclass.
TEST_F(DeviceTest, SetReadOnlyProperty) {
  ::DBus::Error error;
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  EXPECT_FALSE(DBusAdaptor::SetProperty(device_->mutable_store(),
                                        kAddressProperty,
                                        PropertyStoreTest::kStringV,
                                        &error));
  EXPECT_EQ(invalid_args(), error.name());
}

TEST_F(DeviceTest, ClearReadOnlyProperty) {
  ::DBus::Error error;
  EXPECT_FALSE(DBusAdaptor::SetProperty(device_->mutable_store(),
                                        kAddressProperty,
                                        PropertyStoreTest::kStringV,
                                        &error));
}

TEST_F(DeviceTest, ClearReadOnlyDerivedProperty) {
  ::DBus::Error error;
  EXPECT_FALSE(DBusAdaptor::SetProperty(device_->mutable_store(),
                                        kIPConfigsProperty,
                                        PropertyStoreTest::kStringsV,
                                        &error));
}

TEST_F(DeviceTest, DestroyIPConfig) {
  ASSERT_FALSE(device_->ipconfig_.get());
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  device_->DestroyIPConfig();
  ASSERT_FALSE(device_->ipconfig_.get());
}

TEST_F(DeviceTest, DestroyIPConfigNULL) {
  ASSERT_FALSE(device_->ipconfig_.get());
  device_->DestroyIPConfig();
  ASSERT_FALSE(device_->ipconfig_.get());
}

TEST_F(DeviceTest, AcquireIPConfig) {
  device_->ipconfig_ = new IPConfig(control_interface(), "randomname");
  scoped_ptr<MockDHCPProvider> dhcp_provider(new MockDHCPProvider());
  device_->dhcp_provider_ = dhcp_provider.get();
  scoped_refptr<MockDHCPConfig> dhcp_config(new MockDHCPConfig(
                                                    control_interface(),
                                                    kDeviceName));
  EXPECT_CALL(*dhcp_provider, CreateConfig(_, _, _, _))
      .WillOnce(Return(dhcp_config));
  EXPECT_CALL(*dhcp_config, RequestIP())
      .WillOnce(Return(false));
  EXPECT_FALSE(device_->AcquireIPConfig());
  ASSERT_TRUE(device_->ipconfig_.get());
  EXPECT_EQ(kDeviceName, device_->ipconfig_->device_name());
  EXPECT_FALSE(device_->ipconfig_->update_callback_.is_null());
  device_->dhcp_provider_ = NULL;
}

TEST_F(DeviceTest, EnableIPv6) {
  EXPECT_CALL(*device_, SetIPFlag(IPAddress::kFamilyIPv6,
                                  StrEq(Device::kIPFlagDisableIPv6),
                                  StrEq("0")))
      .WillOnce(Return(true));
  device_->EnableIPv6();
}

TEST_F(DeviceTest, EnableIPv6NotAllowed) {
  EXPECT_CALL(*device_, IsIPv6Allowed()).WillOnce(Return(false));
  EXPECT_CALL(*device_, SetIPFlag(_, _, _)).Times(0);
  device_->EnableIPv6();
}

TEST_F(DeviceTest, Load) {
  NiceMock<MockStore> storage;
  const string id = device_->GetStorageIdentifier();
  EXPECT_CALL(storage, ContainsGroup(id)).WillOnce(Return(true));
  EXPECT_CALL(storage, GetBool(id, Device::kStoragePowered, _))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, GetUint64(id, Device::kStorageReceiveByteCount, _))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, GetUint64(id, Device::kStorageTransmitByteCount, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(device_->Load(&storage));
}

TEST_F(DeviceTest, Save) {
  NiceMock<MockStore> storage;
  const string id = device_->GetStorageIdentifier();
  EXPECT_CALL(storage, SetBool(id, Device::kStoragePowered, _))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetUint64(id, Device::kStorageReceiveByteCount, _))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetUint64(id, Device::kStorageTransmitByteCount, _))
      .Times(AtLeast(true));
  EXPECT_TRUE(device_->Save(&storage));
}

TEST_F(DeviceTest, StorageIdGeneration) {
  string to_process("/device/stuff/0");
  ControlInterface::RpcIdToStorageId(&to_process);
  EXPECT_TRUE(isalpha(to_process[0]));
  EXPECT_EQ(string::npos, to_process.find('/'));
}

TEST_F(DeviceTest, SelectedService) {
  EXPECT_FALSE(device_->selected_service_.get());
  device_->SetServiceState(Service::kStateAssociating);
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  manager()));
  SelectService(service);
  EXPECT_TRUE(device_->selected_service_.get() == service.get());

  EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
  device_->SetServiceState(Service::kStateConfiguring);
  EXPECT_CALL(*service, SetFailure(Service::kFailureOutOfRange));
  device_->SetServiceFailure(Service::kFailureOutOfRange);

  // Service should be returned to "Idle" state
  EXPECT_CALL(*service, state())
    .WillOnce(Return(Service::kStateUnknown));
  EXPECT_CALL(*service, SetState(Service::kStateIdle));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  SelectService(NULL);

  // A service in the "Failure" state should not be reset to "Idle"
  SelectService(service);
  EXPECT_CALL(*service, state())
    .WillOnce(Return(Service::kStateFailure));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  SelectService(NULL);
}

TEST_F(DeviceTest, IPConfigUpdatedFailure) {
  scoped_refptr<MockIPConfig> ipconfig = new MockIPConfig(control_interface(),
                                                          kDeviceName);
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  manager()));
  SelectService(service);
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureDHCP,
                                              _,
                                              StrEq("OnIPConfigFailure")));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  EXPECT_CALL(*ipconfig, ResetProperties());
  OnIPConfigFailed(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedFailureWithStatic) {
  scoped_refptr<MockIPConfig> ipconfig = new MockIPConfig(control_interface(),
                                                          kDeviceName);
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  manager()));
  SelectService(service);
  service->static_ip_parameters_.args_.SetString(kAddressProperty, "1.1.1.1");
  service->static_ip_parameters_.args_.SetInt(kPrefixlenProperty, 16);
  // Even though we won't call DisconnectWithFailure, we should still have
  // the service learn from the failed DHCP attempt.
  EXPECT_CALL(*service, DisconnectWithFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service, SetConnection(_)).Times(0);
  // The IPConfig should retain the previous values.
  EXPECT_CALL(*ipconfig, ResetProperties()).Times(0);
  OnIPConfigFailed(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedSuccess) {
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  manager()));
  SelectService(service);
  scoped_refptr<MockIPConfig> ipconfig = new MockIPConfig(control_interface(),
                                                          kDeviceName);
  device_->set_ipconfig(ipconfig);
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*service, IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*service, SetConnection(NotNullRefPtr()));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<string>{ IPConfigMockAdaptor::kRpcId }));

  OnIPConfigUpdated(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedSuccessNoSelectedService) {
  // Make sure shill doesn't crash if a service is disabled immediately
  // after receiving its IP config (selected_service_ is NULL in this case).
  scoped_refptr<MockIPConfig> ipconfig = new MockIPConfig(control_interface(),
                                                          kDeviceName);
  SelectService(NULL);
  OnIPConfigUpdated(ipconfig.get());
}

TEST_F(DeviceTest, OnIPConfigExpired) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  const int kLeaseLength = 1234;
  ipconfig->properties_.lease_duration_seconds = kLeaseLength;

  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.ExpiredLeaseLengthSeconds",
                        kLeaseLength,
                        Metrics::kMetricExpiredLeaseLengthSecondsMin,
                        Metrics::kMetricExpiredLeaseLengthSecondsMax,
                        Metrics::kMetricExpiredLeaseLengthSecondsNumBuckets));

  OnIPConfigExpired(ipconfig.get());
}

TEST_F(DeviceTest, SetEnabledNonPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  StrictMock<MockManager> manager(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  glib());
  SetManager(&manager);
  Error error;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already disabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  device_->SetEnabledNonPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  device_->SetEnabledNonPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(DeviceTest, SetEnabledPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  StrictMock<MockManager> manager(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  glib());
  EXPECT_CALL(manager, UpdateDevice(_));
  SetManager(&manager);
  Error error;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_TRUE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());

  // Disable while already disabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  device_->SetEnabledPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  device_->SetEnabledPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

TEST_F(DeviceTest, Start) {
  EXPECT_FALSE(device_->running_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->SetEnabled(true);
  EXPECT_TRUE(device_->running_);
  EXPECT_TRUE(device_->enabled_pending_);
  device_->OnEnabledStateChanged(ResultCallback(),
                                 Error(Error::kOperationFailed));
  EXPECT_FALSE(device_->enabled_pending_);
}

TEST_F(DeviceTest, Stop) {
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  device_->ipconfig_ = new IPConfig(&control_interface_, kDeviceName);
  scoped_refptr<MockService> service(
      new NiceMock<MockService>(&control_interface_,
                                dispatcher(),
                                metrics(),
                                manager()));
  SelectService(service);

  EXPECT_CALL(*service, state()).
      WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, 0, IFF_UP));
  device_->SetEnabled(false);
  device_->OnEnabledStateChanged(ResultCallback(), Error());

  EXPECT_FALSE(device_->ipconfig_.get());
  EXPECT_FALSE(device_->selected_service_.get());
}

TEST_F(DeviceTest, Reset) {
  Error e;
  device_->Reset(&e, ResultCallback());
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_EQ("Device doesn't support Reset.", e.message());
}

TEST_F(DeviceTest, ResumeWithIPConfig) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);
  EXPECT_CALL(*ipconfig, RenewIP());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithoutIPConfig) {
  // Just test that we don't crash in this case.
  ASSERT_EQ(NULL, device_->ipconfig().get());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithLinkMonitor) {
  MockLinkMonitor *link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  EXPECT_CALL(*link_monitor, OnAfterResume());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithoutLinkMonitor) {
  // Just test that we don't crash in this case.
  EXPECT_FALSE(HasLinkMonitor());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, LinkMonitor) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  MockManager manager(control_interface(),
                      dispatcher(),
                      metrics(),
                      glib());
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  &manager));
  SelectService(service);
  SetConnection(connection.get());
  MockLinkMonitor *link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  SetManager(&manager);
  EXPECT_CALL(*link_monitor, Start()).Times(0);
  EXPECT_CALL(manager, IsTechnologyLinkMonitorEnabled(Technology::kUnknown))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(StartLinkMonitor());

  EXPECT_CALL(*link_monitor, Start())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(StartLinkMonitor());
  EXPECT_TRUE(StartLinkMonitor());

  unsigned int kResponseTime = 123;
  EXPECT_CALL(*link_monitor, GetResponseTimeMilliseconds())
      .WillOnce(Return(kResponseTime));
  {
    Error error;
    EXPECT_EQ(kResponseTime, GetLinkMonitorResponseTime(&error));
    EXPECT_TRUE(error.IsSuccess());
  }
  StopLinkMonitor();
  {
    Error error;
    EXPECT_EQ(0, GetLinkMonitorResponseTime(&error));
    EXPECT_FALSE(error.IsSuccess());
  }
}

TEST_F(DeviceTest, LinkMonitorCancelledOnSelectService) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  MockManager manager(control_interface(),
                      dispatcher(),
                      metrics(),
                      glib());
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  &manager));
  SelectService(service);
  SetConnection(connection.get());
  MockLinkMonitor *link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  SetManager(&manager);
  EXPECT_CALL(*service, state())
      .WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service, SetState(_));
  EXPECT_CALL(*service, SetConnection(_));
  EXPECT_TRUE(HasLinkMonitor());
  SelectService(NULL);
  EXPECT_FALSE(HasLinkMonitor());
}

TEST_F(DeviceTest, TrafficMonitor) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  MockManager manager(control_interface(),
                      dispatcher(),
                      metrics(),
                      glib());
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  &manager));
  SelectService(service);
  SetConnection(connection.get());
  MockTrafficMonitor *traffic_monitor = new StrictMock<MockTrafficMonitor>();
  SetTrafficMonitor(traffic_monitor);  // Passes ownership.
  SetManager(&manager);

  EXPECT_CALL(*device_, IsTrafficMonitorEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(*traffic_monitor, Start());
  StartTrafficMonitor();
  EXPECT_CALL(*traffic_monitor, Stop());
  StopTrafficMonitor();
  Mock::VerifyAndClearExpectations(traffic_monitor);

  EXPECT_CALL(metrics_, NotifyNetworkProblemDetected(_,
      Metrics::kNetworkProblemDNSFailure)).Times(1);
  NetworkProblemDetected(TrafficMonitor::kNetworkProblemDNSFailure);

  // Verify traffic monitor not running when it is disabled.
  traffic_monitor = new StrictMock<MockTrafficMonitor>();
  SetTrafficMonitor(traffic_monitor);
  EXPECT_CALL(*device_, IsTrafficMonitorEnabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*traffic_monitor, Start()).Times(0);
  StartTrafficMonitor();
  EXPECT_CALL(*traffic_monitor, Stop()).Times(0);
  StopTrafficMonitor();
}

TEST_F(DeviceTest, TrafficMonitorCancelledOnSelectService) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  MockManager manager(control_interface(),
                      dispatcher(),
                      metrics(),
                      glib());
  scoped_refptr<MockService> service(
      new StrictMock<MockService>(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  &manager));
  SelectService(service);
  SetConnection(connection.get());
  MockTrafficMonitor *traffic_monitor = new StrictMock<MockTrafficMonitor>();
  SetTrafficMonitor(traffic_monitor);  // Passes ownership.
  EXPECT_CALL(*device_, IsTrafficMonitorEnabled()).WillRepeatedly(Return(true));
  SetManager(&manager);
  EXPECT_CALL(*service, state())
      .WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service, SetState(_));
  EXPECT_CALL(*service, SetConnection(_));
  EXPECT_CALL(*traffic_monitor, Stop());
  SelectService(NULL);
}

TEST_F(DeviceTest, ShouldUseArpGateway) {
  EXPECT_FALSE(device_->ShouldUseArpGateway());
}

TEST_F(DeviceTest, PerformTDLSOperation) {
  EXPECT_EQ("",
            device_->PerformTDLSOperation("do something", "to someone", NULL));
}

TEST_F(DeviceTest, IsConnectedViaTether) {
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // An empty ipconfig doesn't mean we're tethered.
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // Add an ipconfig property that indicates this is an Android tether.
  IPConfig::Properties properties;
  properties.vendor_encapsulated_options =
      Tethering::kAndroidVendorEncapsulatedOptions;
  device_->ipconfig_->UpdateProperties(properties);
  EXPECT_TRUE(device_->IsConnectedViaTether());

  properties.vendor_encapsulated_options = "Some other non-empty value";
  device_->ipconfig_->UpdateProperties(properties);
  EXPECT_FALSE(device_->IsConnectedViaTether());
}

TEST_F(DeviceTest, AvailableIPConfigs) {
  EXPECT_EQ(vector<string>(), device_->AvailableIPConfigs(NULL));
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_EQ(vector<string> { IPConfigMockAdaptor::kRpcId },
            device_->AvailableIPConfigs(NULL));
  device_->ip6config_ = new IPConfig(control_interface(), kDeviceName);

  // We don't really care that the RPC IDs for all IPConfig mock adaptors
  // are the same, or their ordering.  We just need to see that there are two
  // of them when both IPv6 and IPv4 IPConfigs are available.
  EXPECT_EQ(2, device_->AvailableIPConfigs(NULL).size());

  device_->ipconfig_ = NULL;
  EXPECT_EQ(vector<string> { IPConfigMockAdaptor::kRpcId },
            device_->AvailableIPConfigs(NULL));

  device_->ip6config_ = NULL;
  EXPECT_EQ(vector<string>(), device_->AvailableIPConfigs(NULL));
}

TEST_F(DeviceTest, OnIPv6AddressChanged) {
  StrictMock<MockManager> manager(control_interface(),
                                  dispatcher(),
                                  metrics(),
                                  glib());
  manager.set_mock_device_info(&device_info_);
  SetManager(&manager);

  // An IPv6 clear while ip6config_ is NULL will not emit a change.
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _)).Times(0);
  device_->OnIPv6AddressChanged();
  EXPECT_THAT(device_->ip6config_, IsNullRefPtr());
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  IPAddress address0(IPAddress::kFamilyIPv6);
  const char kAddress0[] = "fe80::1aa9:5ff:abcd:1234";
  ASSERT_TRUE(address0.SetAddressFromString(kAddress0));

  // Add an IPv6 address while ip6config_ is NULL.
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(address0), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<string> { IPConfigMockAdaptor::kRpcId }));
  device_->OnIPv6AddressChanged();
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());
  EXPECT_EQ(kAddress0, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // If the IPv6 address does not change, no signal is emitted.
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(address0), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _)).Times(0);
  device_->OnIPv6AddressChanged();
  EXPECT_EQ(kAddress0, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  IPAddress address1(IPAddress::kFamilyIPv6);
  const char kAddress1[] = "fe80::1aa9:5ff:abcd:5678";
  ASSERT_TRUE(address1.SetAddressFromString(kAddress1));

  // If the IPv6 address changes, a signal is emitted.
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(address1), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<string> { IPConfigMockAdaptor::kRpcId }));
  device_->OnIPv6AddressChanged();
  EXPECT_EQ(kAddress1, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // If the IPv6 prefix changes, a signal is emitted.
  address1.set_prefix(64);
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(address1), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<string> { IPConfigMockAdaptor::kRpcId }));
  device_->OnIPv6AddressChanged();
  EXPECT_EQ(kAddress1, device_->ip6config_->properties().address);

  // Return the IPv6 address to NULL.
  EXPECT_CALL(device_info_, GetPrimaryIPv6Address(kDeviceInterfaceIndex, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                            vector<string>()));
  device_->OnIPv6AddressChanged();
  EXPECT_THAT(device_->ip6config_, IsNullRefPtr());
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
}

class DevicePortalDetectionTest : public DeviceTest {
 public:
  DevicePortalDetectionTest()
      : connection_(new StrictMock<MockConnection>(&device_info_)),
        manager_(control_interface(),
                 dispatcher(),
                 metrics(),
                 glib()),
        service_(new StrictMock<MockService>(control_interface(),
                                             dispatcher(),
                                             metrics(),
                                             &manager_)),
        portal_detector_(new StrictMock<MockPortalDetector>(connection_)) {}
    virtual ~DevicePortalDetectionTest() {}
  virtual void SetUp() {
    DeviceTest::SetUp();
    SelectService(service_);
    SetConnection(connection_.get());
    device_->portal_detector_.reset(portal_detector_);  // Passes ownership.
    SetManager(&manager_);
  }

 protected:
  static const int kPortalAttempts;

  bool StartPortalDetection() { return device_->StartPortalDetection(); }
  void StopPortalDetection() { device_->StopPortalDetection(); }

  void PortalDetectorCallback(const PortalDetector::Result &result) {
    device_->PortalDetectorCallback(result);
  }
  bool RequestPortalDetection() {
    return device_->RequestPortalDetection();
  }
  void SetServiceConnectedState(Service::ConnectState state) {
    device_->SetServiceConnectedState(state);
  }
  void ExpectPortalDetectorReset() {
    EXPECT_FALSE(device_->portal_detector_.get());
  }
  void ExpectPortalDetectorSet() {
    EXPECT_TRUE(device_->portal_detector_.get());
  }
  void ExpectPortalDetectorIsMock() {
    EXPECT_EQ(portal_detector_, device_->portal_detector_.get());
  }
  void SetFallbackDNSServerTester(MockDNSServerTester *tester) {
    device_->fallback_dns_server_tester_.reset(tester);
  }
  void InvokeFallbackDNSResultCallback(DNSServerTester::Status status) {
    device_->FallbackDNSResultCallback(status);
  }
  scoped_refptr<MockConnection> connection_;
  StrictMock<MockManager> manager_;
  scoped_refptr<MockService> service_;

  // Used only for EXPECT_CALL().  Object is owned by device.
  MockPortalDetector *portal_detector_;
};

const int DevicePortalDetectionTest::kPortalAttempts = 2;

TEST_F(DevicePortalDetectionTest, ServicePortalDetectionDisabled) {
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, TechnologyPortalDetectionDisabled) {
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionProxyConfig) {
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), HasProxyConfig())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionBadUrl) {
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), HasProxyConfig())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string portal_url;
  EXPECT_CALL(manager_, GetPortalCheckURL())
      .WillRepeatedly(ReturnRef(portal_url));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionStart) {
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), HasProxyConfig())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string portal_url(PortalDetector::kDefaultURL);
  EXPECT_CALL(manager_, GetPortalCheckURL())
      .WillRepeatedly(ReturnRef(portal_url));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline))
      .Times(0);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_.get(), interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_.get(), dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_TRUE(StartPortalDetection());

  // Drop all references to device_info before it falls out of scope.
  SetConnection(NULL);
  StopPortalDetection();
}

TEST_F(DevicePortalDetectionTest, PortalDetectionNonFinal) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .Times(0);
  EXPECT_CALL(*service_.get(), SetState(_))
      .Times(0);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseUnknown,
      PortalDetector::kStatusFailure,
      kPortalAttempts,
      false));
}

TEST_F(DevicePortalDetectionTest, PortalDetectionFailure) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  EXPECT_CALL(metrics_,
              SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                            Metrics::kPortalResultConnectionFailure,
                            Metrics::kPortalResultMax));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline",
                        _, _, _, _)).Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttempts",
                        kPortalAttempts,
                        Metrics::kMetricPortalAttemptsMin,
                        Metrics::kMetricPortalAttemptsMax,
                        Metrics::kMetricPortalAttemptsNumBuckets));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseConnection,
      PortalDetector::kStatusFailure,
      kPortalAttempts,
      true));
}

TEST_F(DevicePortalDetectionTest, PortalDetectionSuccess) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_CALL(metrics_,
              SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                            Metrics::kPortalResultSuccess,
                            Metrics::kPortalResultMax));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline",
                        kPortalAttempts,
                        Metrics::kMetricPortalAttemptsToOnlineMin,
                        Metrics::kMetricPortalAttemptsToOnlineMax,
                        Metrics::kMetricPortalAttemptsToOnlineNumBuckets));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttempts",
                        _, _, _, _)).Times(0);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseContent,
      PortalDetector::kStatusSuccess,
      kPortalAttempts,
      true));
}

TEST_F(DevicePortalDetectionTest, PortalDetectionSuccessAfterFailure) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  EXPECT_CALL(metrics_,
              SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                            Metrics::kPortalResultConnectionFailure,
                            Metrics::kPortalResultMax));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline",
                        _, _, _, _)).Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttempts",
                        kPortalAttempts,
                        Metrics::kMetricPortalAttemptsMin,
                        Metrics::kMetricPortalAttemptsMax,
                        Metrics::kMetricPortalAttemptsNumBuckets));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseConnection,
      PortalDetector::kStatusFailure,
      kPortalAttempts,
      true));
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  EXPECT_CALL(metrics_,
              SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                            Metrics::kPortalResultSuccess,
                            Metrics::kPortalResultMax));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline",
                        kPortalAttempts * 2,
                        Metrics::kMetricPortalAttemptsToOnlineMin,
                        Metrics::kMetricPortalAttemptsToOnlineMax,
                        Metrics::kMetricPortalAttemptsToOnlineNumBuckets));
  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.PortalAttempts",
                        _, _, _, _)).Times(0);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseContent,
      PortalDetector::kStatusSuccess,
      kPortalAttempts,
      true));
}

TEST_F(DevicePortalDetectionTest, RequestPortalDetection) {
  EXPECT_CALL(*service_.get(), state())
      .WillOnce(Return(Service::kStateOnline))
      .WillRepeatedly(Return(Service::kStatePortal));
  EXPECT_FALSE(RequestPortalDetection());

  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(RequestPortalDetection());

  EXPECT_CALL(*portal_detector_, IsInProgress())
      .WillOnce(Return(true));
  // Portal detection already running.
  EXPECT_TRUE(RequestPortalDetection());

  // Make sure our running mock portal detector was not replaced.
  ExpectPortalDetectorIsMock();

  // Throw away our pre-fabricated portal detector, and have the device create
  // a new one.
  StopPortalDetection();
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), HasProxyConfig())
      .WillRepeatedly(Return(false));
  const string kPortalCheckURL("http://portal");
  EXPECT_CALL(manager_, GetPortalCheckURL())
      .WillOnce(ReturnRef(kPortalCheckURL));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_.get(), interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_.get(), dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_TRUE(RequestPortalDetection());
}

TEST_F(DevicePortalDetectionTest, NotConnected) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(false));
  SetServiceConnectedState(Service::kStatePortal);
  // We don't check for the portal detector to be reset here, because
  // it would have been reset as a part of disconnection.
}

TEST_F(DevicePortalDetectionTest, NotPortal) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStateOnline));
  SetServiceConnectedState(Service::kStateOnline);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, NotDefault) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  SetServiceConnectedState(Service::kStatePortal);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, PortalIntervalIsZero) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, GetPortalCheckInterval())
      .WillOnce(Return(0));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  SetServiceConnectedState(Service::kStatePortal);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, RestartPortalDetection) {
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(true));
  const int kPortalDetectionInterval = 10;
  EXPECT_CALL(manager_, GetPortalCheckInterval())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kPortalDetectionInterval));
  const string kPortalCheckURL("http://portal");
  EXPECT_CALL(manager_, GetPortalCheckURL())
      .WillOnce(ReturnRef(kPortalCheckURL));
  EXPECT_CALL(*portal_detector_, StartAfterDelay(kPortalCheckURL,
                                                 kPortalDetectionInterval))
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  SetServiceConnectedState(Service::kStatePortal);
  ExpectPortalDetectorSet();
}

TEST_F(DevicePortalDetectionTest, CancelledOnSelectService) {
  ExpectPortalDetectorSet();
  EXPECT_CALL(*service_.get(), state())
      .WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service_.get(), SetState(_));
  EXPECT_CALL(*service_.get(), SetConnection(_));
  SelectService(NULL);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, PortalDetectionDNSFailure) {
  // Setup dns server tester.
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_.get(), interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  MockDNSServerTester *dns_server_tester =
      new MockDNSServerTester(connection_);
  SetFallbackDNSServerTester(dns_server_tester);

  // DNS Failure, DNS server tester is started.
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  EXPECT_CALL(*dns_server_tester, Start()).Times(1);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseDNS,
      PortalDetector::kStatusFailure,
      kPortalAttempts,
      true));
  Mock::VerifyAndClearExpectations(dns_server_tester);

  // DNS Timeout, DNS server tester is started.
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  EXPECT_CALL(*dns_server_tester, Start()).Times(1);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseDNS,
      PortalDetector::kStatusTimeout,
      kPortalAttempts,
      true));
  Mock::VerifyAndClearExpectations(dns_server_tester);

  // Other Failure, DNS server tester not started.
  EXPECT_CALL(*service_.get(), IsConnected())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), SetState(Service::kStatePortal));
  EXPECT_CALL(*connection_.get(), is_default())
      .WillOnce(Return(false));
  EXPECT_CALL(*dns_server_tester, Start()).Times(0);
  PortalDetectorCallback(PortalDetector::Result(
      PortalDetector::kPhaseConnection,
      PortalDetector::kStatusFailure,
      kPortalAttempts,
      true));
  Mock::VerifyAndClearExpectations(dns_server_tester);
}

TEST_F(DevicePortalDetectionTest, FallbackDNSResultCallback) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);

  // Fallback DNS test failed.
  EXPECT_CALL(*connection_.get(), UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(metrics_,
      NotifyFallbackDNSTestResult(_, Metrics::kFallbackDNSTestResultFailure))
          .Times(1);
  InvokeFallbackDNSResultCallback(DNSServerTester::kStatusFailure);
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig);
  Mock::VerifyAndClearExpectations(&metrics_);

  // Fallback DNS test succeed with auto fallback disabled.
  EXPECT_CALL(*service_.get(), is_dns_auto_fallback_allowed())
      .WillOnce(Return(false));
  EXPECT_CALL(*connection_.get(), UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*service_.get(), NotifyIPConfigChanges()).Times(0);
  EXPECT_CALL(metrics_,
      NotifyFallbackDNSTestResult(_, Metrics::kFallbackDNSTestResultSuccess))
          .Times(1);
  InvokeFallbackDNSResultCallback(DNSServerTester::kStatusSuccess);
  Mock::VerifyAndClearExpectations(service_.get());
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig);
  Mock::VerifyAndClearExpectations(&metrics_);

  // Fallback DNS test succeed with auto fallback enabled.
  EXPECT_CALL(*service_.get(), is_dns_auto_fallback_allowed())
      .WillOnce(Return(true));
  EXPECT_CALL(*service_.get(), IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_.get(), IsPortalDetectionAuto())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_.get(), HasProxyConfig())
      .WillRepeatedly(Return(false));
  const string kPortalCheckURL("http://portal");
  EXPECT_CALL(manager_, GetPortalCheckURL())
      .WillOnce(ReturnRef(kPortalCheckURL));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_.get(), interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_.get(), dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));

  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*connection_.get(), UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*service_.get(), NotifyIPConfigChanges()).Times(1);
  EXPECT_CALL(metrics_,
      NotifyFallbackDNSTestResult(_, Metrics::kFallbackDNSTestResultSuccess))
          .Times(1);
  InvokeFallbackDNSResultCallback(DNSServerTester::kStatusSuccess);
  Mock::VerifyAndClearExpectations(service_.get());
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig);
  Mock::VerifyAndClearExpectations(&metrics_);
}

class DeviceByteCountTest : public DeviceTest {
 public:
  DeviceByteCountTest()
      : manager_(control_interface(),
                 dispatcher(),
                 metrics(),
                 glib()),
        rx_byte_count_(0),
        tx_byte_count_(0),
        rx_stored_byte_count_(0),
        tx_stored_byte_count_(0) {}
  virtual ~DeviceByteCountTest() {}

  virtual void SetUp() {
    DeviceTest::SetUp();
    EXPECT_CALL(manager_, device_info()).WillRepeatedly(Return(&device_info_));
    EXPECT_CALL(device_info_, GetByteCounts(kDeviceInterfaceIndex, _, _))
        .WillRepeatedly(Invoke(this, &DeviceByteCountTest::ReturnByteCounts));
    const string id = device_->GetStorageIdentifier();
    EXPECT_CALL(storage_, ContainsGroup(id)).WillRepeatedly(Return(true));
    EXPECT_CALL(storage_, GetUint64(id, Device::kStorageReceiveByteCount, _))
        .WillRepeatedly(
            Invoke(this, &DeviceByteCountTest::GetStoredReceiveCount));
    EXPECT_CALL(storage_, GetUint64(id, Device::kStorageTransmitByteCount, _))
        .WillRepeatedly(
            Invoke(this, &DeviceByteCountTest::GetStoredTransmitCount));
  }

  bool ReturnByteCounts(int interface_index, uint64 *rx, uint64 *tx) {
    *rx = rx_byte_count_;
    *tx = tx_byte_count_;
    return true;
  }

  bool ExpectByteCounts(DeviceRefPtr device,
                        int64 expected_rx, int64 expected_tx) {
    int64 actual_rx = device->GetReceiveByteCount();
    int64 actual_tx = device->GetTransmitByteCount();
    EXPECT_EQ(expected_rx, actual_rx);
    EXPECT_EQ(expected_tx, actual_tx);
    return expected_rx == actual_rx && expected_tx == actual_tx;
  }

  void ExpectSavedCounts(DeviceRefPtr device,
                         int64 expected_rx, int64 expected_tx) {
    EXPECT_CALL(storage_,
        SetUint64(_, Device::kStorageReceiveByteCount, expected_rx))
        .WillOnce(Return(true));
    EXPECT_CALL(storage_,
        SetUint64(_, Device::kStorageTransmitByteCount, expected_tx))
        .WillOnce(Return(true));
    EXPECT_TRUE(device->Save(&storage_));
  }


  bool GetStoredReceiveCount(const string &group, const string &key,
                             uint64 *value) {
    if (!rx_stored_byte_count_) {
      return false;
    }
    *value = rx_stored_byte_count_;
    return true;
  }

  bool GetStoredTransmitCount(const string &group, const string &key,
                              uint64 *value) {
    if (!tx_stored_byte_count_) {
      return false;
    }
    *value = tx_stored_byte_count_;
    return true;
  }

 protected:
  NiceMock<MockManager> manager_;
  NiceMock<MockStore> storage_;
  uint64 rx_byte_count_;
  uint64 tx_byte_count_;
  uint64 rx_stored_byte_count_;
  uint64 tx_stored_byte_count_;
};


TEST_F(DeviceByteCountTest, GetByteCounts) {
  // On Device initialization, byte counts should be zero, independent of
  // the byte counts reported by the interface.
  rx_byte_count_ = 123;
  tx_byte_count_ = 456;
  DeviceRefPtr device(new TestDevice(control_interface(),
                                     dispatcher(),
                                     NULL,
                                     &manager_,
                                     kDeviceName,
                                     kDeviceAddress,
                                     kDeviceInterfaceIndex,
                                     Technology::kUnknown));
  EXPECT_TRUE(ExpectByteCounts(device, 0, 0));

  // Device should report any increase in the byte counts reported in the
  // interface.
  const int64 delta_rx_count = 789;
  const int64 delta_tx_count = 12;
  rx_byte_count_ += delta_rx_count;
  tx_byte_count_ += delta_tx_count;
  EXPECT_TRUE(ExpectByteCounts(device, delta_rx_count, delta_tx_count));

  // Expect the correct values to be saved to the profile.
  ExpectSavedCounts(device, delta_rx_count, delta_tx_count);

  // If Device is loaded from a profile that does not contain stored byte
  // counts, the byte counts reported should remain unchanged.
  EXPECT_TRUE(device->Load(&storage_));
  EXPECT_TRUE(ExpectByteCounts(device, delta_rx_count, delta_tx_count));

  // If Device is loaded from a profile that contains stored byte
  // counts, the byte counts reported should now reflect the stored values.
  rx_stored_byte_count_ = 345;
  tx_stored_byte_count_ = 678;
  EXPECT_TRUE(device->Load(&storage_));
  EXPECT_TRUE(ExpectByteCounts(
      device, rx_stored_byte_count_, tx_stored_byte_count_));

  // Increases to the interface receive count should be reflected as offsets
  // to the stored byte counts.
  rx_byte_count_ += delta_rx_count;
  tx_byte_count_ += delta_tx_count;
  EXPECT_TRUE(ExpectByteCounts(device,
                               rx_stored_byte_count_ + delta_rx_count,
                               tx_stored_byte_count_ + delta_tx_count));

  // Expect the correct values to be saved to the profile.
  ExpectSavedCounts(device,
                    rx_stored_byte_count_ + delta_rx_count,
                    tx_stored_byte_count_ + delta_tx_count);

  // Expect that after resetting byte counts, read-back values return to zero,
  // and that the device requests this information to be persisted.
  EXPECT_CALL(manager_, UpdateDevice(device));
  device->ResetByteCounters();
  EXPECT_TRUE(ExpectByteCounts(device, 0, 0));
}

}  // namespace shill
