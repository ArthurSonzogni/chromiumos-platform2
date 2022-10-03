// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <ctype.h>
#include <linux/if.h>  // NOLINT - Needs typedefs from sys/socket.h.
#include <sys/socket.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/check.h>
#include <base/run_loop.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/event_dispatcher.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_ipconfig.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_portal_detector.h"
#include "shill/mock_service.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/mock_time.h"
#include "shill/net/ndisc.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/mock_network.h"
#include "shill/network/network.h"
#include "shill/portal_detector.h"
#include "shill/routing_table.h"
#include "shill/store/fake_store.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/tethering.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace shill {

namespace {
MATCHER_P(IsWeakPtrTo, address, "") {
  return arg.get() == address;
}
}  // namespace

class TestDevice : public Device {
 public:
  TestDevice(Manager* manager,
             const std::string& link_name,
             const std::string& address,
             int interface_index,
             Technology technology)
      : Device(manager, link_name, address, interface_index, technology),
        start_stop_error_(Error::kSuccess) {
    ON_CALL(*this, ShouldBringNetworkInterfaceDownAfterDisabled())
        .WillByDefault(Invoke(
            this,
            &TestDevice::DeviceShouldBringNetworkInterfaceDownAfterDisabled));
  }

  ~TestDevice() override = default;

  void Start(const EnabledStateChangedCallback& callback) override {
    callback.Run(start_stop_error_);
  }

  void Stop(const EnabledStateChangedCallback& callback) override {
    callback.Run(start_stop_error_);
  }

  MOCK_METHOD(bool,
              ShouldBringNetworkInterfaceDownAfterDisabled,
              (),
              (const, override));
  MOCK_METHOD(void,
              StartConnectionDiagnosticsAfterPortalDetection,
              (),
              (override));

  bool DeviceShouldBringNetworkInterfaceDownAfterDisabled() const {
    return Device::ShouldBringNetworkInterfaceDownAfterDisabled();
  }

  void device_set_mac_address(const std::string& mac_address) {
    Device::set_mac_address(mac_address);
  }

  Error start_stop_error_;
};

class DeviceTest : public testing::Test {
 public:
  DeviceTest()
      : manager_(control_interface(), dispatcher(), metrics()),
        device_info_(manager()) {
    manager()->set_mock_device_info(&device_info_);
    DHCPProvider::GetInstance()->control_interface_ = control_interface();
    DHCPProvider::GetInstance()->dispatcher_ = dispatcher();

    auto client = std::make_unique<patchpanel::FakeClient>();
    patchpanel_client_ = client.get();
    manager_.patchpanel_client_ = std::move(client);

    device_ =
        new NiceMock<TestDevice>(manager(), kDeviceName, kDeviceAddress,
                                 kDeviceInterfaceIndex, Technology::kUnknown);
  }
  ~DeviceTest() override = default;

  void SetUp() override {
    device_->rtnl_handler_ = &rtnl_handler_;
    RoutingTable::GetInstance()->Start();
  }

 protected:
  static const char kDeviceName[];
  static const char kDeviceAddress[];
  static const int kDeviceInterfaceIndex;

  void CreateMockNetwork() {
    auto network = std::make_unique<NiceMock<MockNetwork>>(
        kDeviceInterfaceIndex, kDeviceName, Technology::kUnknown);
    network_ = network.get();
    device_->set_network_for_testing(std::move(network));
  }

  void OnIPv4ConfigUpdated() { device_->network()->OnIPv4ConfigUpdated(); }

  void OnDHCPFailure() { device_->network()->OnDHCPFailure(); }

  patchpanel::TrafficCounter CreateCounter(
      const std::valarray<uint64_t>& vals,
      patchpanel::TrafficCounter::Source source,
      const std::string& device_name) {
    EXPECT_EQ(4, vals.size());
    patchpanel::TrafficCounter counter;
    counter.set_rx_bytes(vals[0]);
    counter.set_tx_bytes(vals[1]);
    counter.set_rx_packets(vals[2]);
    counter.set_tx_packets(vals[3]);
    counter.set_source(source);
    counter.set_device(device_name);
    return counter;
  }

  void SelectService(scoped_refptr<MockService> service) {
    if (service) {
      EXPECT_CALL(*service,
                  SetAttachedNetwork(IsWeakPtrTo(device_->network())));
    }
    device_->SelectService(service);
  }

  void SetConnection(std::unique_ptr<Connection> connection) {
    device_->network_->set_state_for_testing(
        connection ? Network::State::kConnected : Network::State::kIdle);
    device_->network_->set_connection_for_testing(std::move(connection));
  }

  MockPortalDetector* SetMockPortalDetector() {
    auto mock_portal_detector =
        std::make_unique<StrictMock<MockPortalDetector>>();
    MockPortalDetector* mock_portal_detectorp = mock_portal_detector.get();
    EXPECT_CALL(*mock_portal_detectorp, IsInProgress())
        .WillRepeatedly(Return(false));
    device_->portal_detector_ = std::move(mock_portal_detector);
    return mock_portal_detectorp;
  }

  DeviceMockAdaptor* GetDeviceMockAdaptor() {
    return static_cast<DeviceMockAdaptor*>(device_->adaptor_.get());
  }

  PortalDetector* GetPortalDetector() {
    return device_->portal_detector_.get();
  }

  MockControl* control_interface() { return &control_interface_; }
  EventDispatcher* dispatcher() { return &dispatcher_; }
  MockMetrics* metrics() { return &metrics_; }
  MockManager* manager() { return &manager_; }

  void SetupIPv6Config() {
    const char kAddress[] = "2001:db8::1";
    const char kDnsServer1[] = "2001:db8::2";
    const char kDnsServer2[] = "2001:db8::3";
    IPConfig::Properties properties;
    properties.address = kAddress;
    properties.dns_servers = {kDnsServer1, kDnsServer2};

    device_->set_ip6config(std::make_unique<NiceMock<MockIPConfig>>(
        control_interface(), kDeviceName));
    device_->ip6config()->set_properties(properties);
  }

  void TriggerConnectionUpdate() {
    EXPECT_CALL(*network_, IsConnected()).WillRepeatedly(Return(true));
    network_->set_ipconfig(
        std::make_unique<MockIPConfig>(control_interface(), kDeviceName));
    device_->OnConnectionUpdated();
    device_->OnIPConfigsPropertyUpdated();
  }

  void SetDHCPProvider(DHCPProvider* dhcp_provider) {
    device_->network()->set_dhcp_provider_for_testing(dhcp_provider);
  }

  static ManagerProperties MakePortalProperties() {
    ManagerProperties props;
    props.portal_http_url = PortalDetector::kDefaultHttpUrl;
    props.portal_https_url = PortalDetector::kDefaultHttpsUrl;
    props.portal_fallback_http_urls = std::vector<std::string>(
        PortalDetector::kDefaultFallbackHttpUrls.begin(),
        PortalDetector::kDefaultFallbackHttpUrls.end());
    return props;
  }

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;

  scoped_refptr<TestDevice> device_;
  NiceMock<MockDeviceInfo> device_info_;
  MockTime time_;
  StrictMock<MockRTNLHandler> rtnl_handler_;
  patchpanel::FakeClient* patchpanel_client_;
  MockIPConfig* ipconfig_;  // owned by |device_|
  MockNetwork* network_;    // owned by |device_|
};

const char DeviceTest::kDeviceName[] = "testdevice";
const char DeviceTest::kDeviceAddress[] = "address";
const int DeviceTest::kDeviceInterfaceIndex = 0;

TEST_F(DeviceTest, Contains) {
  EXPECT_TRUE(device_->store().Contains(kNameProperty));
  EXPECT_FALSE(device_->store().Contains(""));
}

TEST_F(DeviceTest, GetProperties) {
  brillo::VariantDictionary props;
  Error error;
  device_->store().GetProperties(&props, &error);
  ASSERT_FALSE(props.find(kNameProperty) == props.end());
  EXPECT_TRUE(props[kNameProperty].IsTypeCompatible<std::string>());
  EXPECT_EQ(props[kNameProperty].Get<std::string>(), std::string(kDeviceName));
}

// Note: there are currently no writeable Device properties that
// aren't registered in a subclass.
TEST_F(DeviceTest, SetReadOnlyProperty) {
  Error error;
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  device_->mutable_store()->SetAnyProperty(kAddressProperty,
                                           brillo::Any(std::string()), &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

TEST_F(DeviceTest, ClearReadOnlyProperty) {
  Error error;
  device_->mutable_store()->SetAnyProperty(kAddressProperty,
                                           brillo::Any(std::string()), &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

TEST_F(DeviceTest, ClearReadOnlyDerivedProperty) {
  Error error;
  device_->mutable_store()->SetAnyProperty(kIPConfigsProperty,
                                           brillo::Any(Strings()), &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

TEST_F(DeviceTest, Load) {
  device_->enabled_persistent_ = false;

  FakeStore storage;
  const auto id = device_->GetStorageIdentifier();
  storage.SetBool(id, Device::kStoragePowered, true);
  EXPECT_TRUE(device_->Load(&storage));
  EXPECT_TRUE(device_->enabled_persistent());
}

TEST_F(DeviceTest, Save) {
  device_->enabled_persistent_ = true;

  FakeStore storage;
  EXPECT_TRUE(device_->Save(&storage));
  const auto id = device_->GetStorageIdentifier();
  bool powered = false;
  EXPECT_TRUE(storage.GetBool(id, Device::kStoragePowered, &powered));
  EXPECT_TRUE(powered);
}

TEST_F(DeviceTest, SelectedService) {
  EXPECT_EQ(nullptr, device_->selected_service_);
  device_->SetServiceState(Service::kStateAssociating);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_EQ(device_->selected_service_, service);

  EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
  device_->SetServiceState(Service::kStateConfiguring);
  EXPECT_CALL(*service, SetFailure(Service::kFailureOutOfRange));
  device_->SetServiceFailure(Service::kFailureOutOfRange);

  // Service should be returned to "Idle" state
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateUnknown));
  EXPECT_CALL(*service, SetState(Service::kStateIdle));
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  SelectService(nullptr);

  // A service in the "Failure" state should not be reset to "Idle"
  SelectService(service);
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateFailure));
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  SelectService(nullptr);
}

TEST_F(DeviceTest, ResetConnection) {
  EXPECT_EQ(nullptr, device_->selected_service_);
  device_->SetServiceState(Service::kStateAssociating);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_EQ(device_->selected_service_, service);

  // ResetConnection() should drop the connection and the selected service,
  // but should not change the service state.
  EXPECT_CALL(*service, SetState(_)).Times(0);
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  device_->ResetConnection();
  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, NetworkFailure) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureDHCP, _,
                                              HasSubstr("OnIPConfigFailure")));
  device_->OnNetworkStopped(/*is_failure=*/true);
}

TEST_F(DeviceTest, ConnectionUpdated) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_CALL(*service, IsConnected(nullptr))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));

  TriggerConnectionUpdate();
}

TEST_F(DeviceTest, ConnectionUpdatedAlreadyOnline) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  // The service is already Online and selected, so it should not transition
  // back to Connected.
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_CALL(*service, SetState(Service::kStateConnected)).Times(0);
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));

  // Successful portal (non-)detection forces the service Online.
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));

  TriggerConnectionUpdate();
}

TEST_F(DeviceTest, ConnectionUpdatedSuccessNoSelectedService) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  // Make sure shill doesn't crash if a service is disabled immediately after
  // Network is connected (selected_service_ is nullptr in this case).
  SelectService(nullptr);
  TriggerConnectionUpdate();
}

TEST_F(DeviceTest, SetEnabledNonPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  Error error;
  SetEnabledSync(device_.get(), true, false, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  SetEnabledSync(device_.get(), true, false, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  SetEnabledSync(device_.get(), true, false, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already disabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  SetEnabledSync(device_.get(), false, false, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  SetEnabledSync(device_.get(), false, false, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(DeviceTest, SetEnabledPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  Error error;
  SetEnabledSync(device_.get(), true, true, &error);
  EXPECT_TRUE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled (but not persisted).
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  SetEnabledSync(device_.get(), true, true, &error);
  EXPECT_TRUE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  SetEnabledSync(device_.get(), true, true, &error);
  EXPECT_TRUE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());

  // Disable while already disabled (persisted).
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  SetEnabledSync(device_.get(), false, true, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  SetEnabledSync(device_.get(), false, true, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());

  // Disable while already disabled (but not persisted).
  error.Reset();
  device_->enabled_persistent_ = true;
  device_->enabled_pending_ = false;
  device_->enabled_ = false;
  SetEnabledSync(device_.get(), false, true, &error);
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(DeviceTest, Start) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->SetEnabled(true);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
}

TEST_F(DeviceTest, StartFailure) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->start_stop_error_.Populate(Error::kOperationFailed);
  device_->SetEnabled(true);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
}

TEST_F(DeviceTest, Stop) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, 0, IFF_UP));
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  EXPECT_CALL(*network_, Stop());
  device_->SetEnabled(false);

  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StopWithFixedIpParams) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  device_->network()->set_fixed_ip_params_for_testing(true);
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, _, _)).Times(0);
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  EXPECT_CALL(*network_, Stop());
  device_->SetEnabled(false);

  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StopWithNetworkInterfaceDisabledAfterward) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*device_, ShouldBringNetworkInterfaceDownAfterDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(*service, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, 0, IFF_UP));
  EXPECT_CALL(*network_, Stop());
  device_->SetEnabled(false);

  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StartProhibited) {
  DeviceRefPtr device(new TestDevice(manager(), kDeviceName, kDeviceAddress,
                                     kDeviceInterfaceIndex, Technology::kWiFi));
  {
    Error error;
    manager()->SetProhibitedTechnologies("wifi", &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  device->SetEnabled(true);
  EXPECT_FALSE(device->enabled_pending());

  {
    Error error;
    manager()->SetProhibitedTechnologies("", &error);
    EXPECT_TRUE(error.IsSuccess());
  }
  device->SetEnabled(true);
  EXPECT_TRUE(device->enabled_pending());
}

TEST_F(DeviceTest, Reset) {
  base::RunLoop run_loop;
  Error e;
  device_->Reset(
      base::BindRepeating(&SetErrorAndReturn, run_loop.QuitClosure(), &e));
  run_loop.Run();

  EXPECT_EQ(Error::kNotImplemented, e.type());
}

TEST_F(DeviceTest, Resume) {
  // TODO(b/232177767): Remove this after all tests are based on MockNetwork.
  CreateMockNetwork();
  EXPECT_CALL(*network_, RenewDHCPLease());
  EXPECT_CALL(*network_, InvalidateIPv6Config());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, IsConnectedViaTether) {
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // An empty ipconfig doesn't mean we're tethered.
  device_->set_ipconfig(
      std::make_unique<IPConfig>(control_interface(), kDeviceName));
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // Add an ipconfig property that indicates this is an Android tether.
  IPConfig::Properties properties;
  properties.vendor_encapsulated_options =
      ByteArray(Tethering::kAndroidVendorEncapsulatedOptions,
                Tethering::kAndroidVendorEncapsulatedOptions +
                    strlen(Tethering::kAndroidVendorEncapsulatedOptions));
  device_->ipconfig()->UpdateProperties(properties);
  EXPECT_TRUE(device_->IsConnectedViaTether());

  const char kTestVendorEncapsulatedOptions[] = "Some other non-empty value";
  properties.vendor_encapsulated_options = ByteArray(
      kTestVendorEncapsulatedOptions,
      kTestVendorEncapsulatedOptions + sizeof(kTestVendorEncapsulatedOptions));
  device_->ipconfig()->UpdateProperties(properties);
  EXPECT_FALSE(device_->IsConnectedViaTether());
}

TEST_F(DeviceTest, AvailableIPConfigs) {
  EXPECT_EQ(std::vector<RpcIdentifier>(), device_->AvailableIPConfigs(nullptr));
  device_->set_ipconfig(
      std::make_unique<IPConfig>(control_interface(), kDeviceName));
  EXPECT_EQ(std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId},
            device_->AvailableIPConfigs(nullptr));
  device_->set_ip6config(
      std::make_unique<IPConfig>(control_interface(), kDeviceName));

  // We don't really care that the RPC IDs for all IPConfig mock adaptors
  // are the same, or their ordering.  We just need to see that there are two
  // of them when both IPv6 and IPv4 IPConfigs are available.
  EXPECT_EQ(2, device_->AvailableIPConfigs(nullptr).size());

  device_->set_ipconfig(nullptr);
  EXPECT_EQ(std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId},
            device_->AvailableIPConfigs(nullptr));

  device_->set_ip6config(nullptr);
  EXPECT_EQ(std::vector<RpcIdentifier>(), device_->AvailableIPConfigs(nullptr));
}

TEST_F(DeviceTest, OnIPv6AddressChanged) {
  // An IPv6 clear while ip6config_ is nullptr will not emit a change.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->network()->OnIPv6AddressChanged(nullptr);
  EXPECT_EQ(device_->ip6config(), nullptr);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  IPAddress address0(IPAddress::kFamilyIPv6);
  const char kAddress0[] = "fe80::1aa9:5ff:abcd:1234";
  ASSERT_TRUE(address0.SetAddressFromString(kAddress0));

  // Add an IPv6 address while ip6config_ is nullptr.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6AddressChanged(&address0);
  EXPECT_NE(device_->ip6config(), nullptr);
  EXPECT_EQ(kAddress0, device_->ip6config()->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  // If the IPv6 address does not change, no signal is emitted.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->network()->OnIPv6AddressChanged(&address0);
  EXPECT_EQ(kAddress0, device_->ip6config()->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  IPAddress address1(IPAddress::kFamilyIPv6);
  const char kAddress1[] = "fe80::1aa9:5ff:abcd:5678";
  ASSERT_TRUE(address1.SetAddressFromString(kAddress1));

  // If the IPv6 address changes, a signal is emitted.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6AddressChanged(&address1);
  EXPECT_EQ(kAddress1, device_->ip6config()->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  // If the IPv6 prefix changes, a signal is emitted.
  address1.set_prefix(64);
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6AddressChanged(&address1);
  EXPECT_EQ(kAddress1, device_->ip6config()->properties().address);

  // Return the IPv6 address to nullptr.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                            std::vector<RpcIdentifier>()));
  device_->network()->OnIPv6AddressChanged(nullptr);
  EXPECT_EQ(device_->ip6config(), nullptr);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
}

TEST_F(DeviceTest, OnIPv6DnsServerAddressesChanged) {
  // With existing IPv4 connection, so no attempt to setup IPv6 connection.
  // IPv6 connection is being tested in OnIPv6ConfigurationCompleted test.
  auto* connection = new StrictMock<MockConnection>(&device_info_);
  SetConnection(std::unique_ptr<Connection>(connection));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));

  // IPv6 DNS server addresses are not provided will not emit a change.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(device_->ip6config(), nullptr);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  const char kAddress1[] = "fe80::1aa9:5ff:abcd:1234";
  const char kAddress2[] = "fe80::1aa9:5ff:abcd:1235";
  const uint32_t kInfiniteLifetime = 0xffffffff;
  IPAddress ipv6_address1(IPAddress::kFamilyIPv6);
  IPAddress ipv6_address2(IPAddress::kFamilyIPv6);
  ASSERT_TRUE(ipv6_address1.SetAddressFromString(kAddress1));
  ASSERT_TRUE(ipv6_address2.SetAddressFromString(kAddress2));
  std::vector<IPAddress> dns_server_addresses = {ipv6_address1, ipv6_address2};
  std::vector<std::string> dns_server_addresses_str = {kAddress1, kAddress2};

  // Add IPv6 DNS server addresses while ip6config_ is nullptr.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kInfiniteLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_NE(device_->ip6config(), nullptr);
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Add an IPv6 address while IPv6 DNS server addresses already existed.
  IPAddress address3(IPAddress::kFamilyIPv6);
  const char kAddress3[] = "fe80::1aa9:5ff:abcd:1236";
  ASSERT_TRUE(address3.SetAddressFromString(kAddress3));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6AddressChanged(&address3);
  EXPECT_NE(device_->ip6config(), nullptr);
  EXPECT_EQ(kAddress3, device_->ip6config()->properties().address);
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // If the IPv6 DNS server addresses does not change, no signal is emitted.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kInfiniteLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Setting lifetime to 0 should expire and clear out the DNS server.
  const uint32_t kExpiredLifetime = 0;
  std::vector<std::string> empty_dns_server;
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kExpiredLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(empty_dns_server, device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Set DNS server with lifetime of 1 hour.
  const uint32_t kLifetimeOneHr = 3600;
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kLifetimeOneHr), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Return the DNS server addresses to nullptr.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->network()->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(empty_dns_server, device_->ip6config()->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
}

TEST_F(DeviceTest, OnIPv6ConfigurationCompleted) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  auto* connection = new StrictMock<MockConnection>(&device_info_);
  SetConnection(std::unique_ptr<Connection>(connection));

  // Setup initial IPv6 configuration.
  SetupIPv6Config();
  EXPECT_NE(device_->ip6config(), nullptr);

  // IPv6 configuration update with non-IPv6 connection, no connection update.
  // TODO(b/232177767): Check Network state instead.
  EXPECT_TRUE(device_->network()->IsConnected());
  IPAddress address1(IPAddress::kFamilyIPv6);
  const char kAddress1[] = "fe80::1aa9:5ff:abcd:1231";
  ASSERT_TRUE(address1.SetAddressFromString(kAddress1));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
  device_->network()->OnIPv6AddressChanged(&address1);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
  Mock::VerifyAndClearExpectations(service.get());
  Mock::VerifyAndClearExpectations(connection);

  // IPv6 configuration update with IPv6 connection, connection update.
  IPAddress address2(IPAddress::kFamilyIPv6);
  const char kAddress2[] = "fe80::1aa9:5ff:abcd:1232";
  ASSERT_TRUE(address2.SetAddressFromString(kAddress2));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  std::vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(true));
  EXPECT_CALL(*connection,
              UpdateFromIPConfig(Ref(device_->ip6config()->properties())));
  EXPECT_CALL(*service, IsConnected(nullptr))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  device_->network()->OnIPv6AddressChanged(&address2);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
  Mock::VerifyAndClearExpectations(service.get());
  Mock::VerifyAndClearExpectations(connection);
}

TEST_F(DeviceTest, SetMacAddress) {
  constexpr char mac_address[] = "abcdefabcdef";
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitStringChanged(kAddressProperty, mac_address));
  EXPECT_NE(mac_address, device_->mac_address());
  device_->device_set_mac_address(mac_address);
  EXPECT_EQ(mac_address, device_->mac_address());
}

TEST_F(DeviceTest, FetchTrafficCounters) {
  auto source0 = patchpanel::TrafficCounter::CHROME;
  auto source1 = patchpanel::TrafficCounter::USER;
  std::valarray<uint64_t> counter_arr0{2842, 1243, 240598, 43095};
  std::valarray<uint64_t> counter_arr1{4554666, 43543, 5999, 500000};
  patchpanel::TrafficCounter counter0 =
      CreateCounter(counter_arr0, source0, kDeviceName);
  patchpanel::TrafficCounter counter1 =
      CreateCounter(counter_arr1, source1, kDeviceName);
  std::vector<patchpanel::TrafficCounter> counters{counter0, counter1};
  patchpanel_client_->set_stored_traffic_counters(counters);

  EXPECT_EQ(nullptr, device_->selected_service_);
  scoped_refptr<MockService> service0(new NiceMock<MockService>(manager()));
  EXPECT_TRUE(service0->traffic_counter_snapshot_.empty());
  EXPECT_TRUE(service0->current_traffic_counters_.empty());
  SelectService(service0);
  EXPECT_EQ(service0, device_->selected_service_);
  EXPECT_TRUE(service0->current_traffic_counters_.empty());
  EXPECT_EQ(2, service0->traffic_counter_snapshot_.size());
  for (size_t i = 0; i < Service::kTrafficCounterArraySize; i++) {
    EXPECT_EQ(counter_arr0[i], service0->traffic_counter_snapshot_[source0][i]);
    EXPECT_EQ(counter_arr1[i], service0->traffic_counter_snapshot_[source1][i]);
  }

  std::valarray<uint64_t> counter_diff0{12, 98, 34, 76};
  std::valarray<uint64_t> counter_diff1{324534, 23434, 785676, 256};
  std::valarray<uint64_t> new_total0 = counter_arr0 + counter_diff0;
  std::valarray<uint64_t> new_total1 = counter_arr1 + counter_diff1;
  counter0 = CreateCounter(new_total0, source0, kDeviceName);
  counter1 = CreateCounter(new_total1, source1, kDeviceName);
  counters = {counter0, counter1};
  patchpanel_client_->set_stored_traffic_counters(counters);

  scoped_refptr<MockService> service1(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*service0, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  SelectService(service1);
  EXPECT_EQ(service1, device_->selected_service_);
  for (size_t i = 0; i < Service::kTrafficCounterArraySize; i++) {
    EXPECT_EQ(counter_diff0[i],
              service0->current_traffic_counters_[source0][i]);
    EXPECT_EQ(counter_diff1[i],
              service0->current_traffic_counters_[source1][i]);

    EXPECT_EQ(new_total0[i], service1->traffic_counter_snapshot_[source0][i]);
    EXPECT_EQ(new_total1[i], service1->traffic_counter_snapshot_[source1][i]);
  }
  EXPECT_TRUE(service1->current_traffic_counters_.empty());
}

class DevicePortalDetectionTest : public DeviceTest {
 public:
  DevicePortalDetectionTest()
      : service_(new StrictMock<MockService>(manager())) {
    CreateMockNetwork();
    ON_CALL(*network_, IsConnected()).WillByDefault(Return(true));
  }
  ~DevicePortalDetectionTest() override = default;

  void SetUp() override {
    DeviceTest::SetUp();
    EXPECT_CALL(*service_, SetProbeUrl(_)).Times(AnyNumber());
    SelectService(service_);
  }

  void TearDown() override { device_->portal_detector_.reset(); }

 protected:
  static const int kPortalAttempts;

  bool UpdatePortalDetector(bool restart = false) {
    return device_->UpdatePortalDetector(restart);
  }
  void StopPortalDetection() { device_->StopPortalDetection(); }

  void PortalDetectorCallback(const PortalDetector::Result& result) {
    device_->PortalDetectorCallback(result);
  }

  scoped_refptr<MockService> service_;
};

const int DevicePortalDetectionTest::kPortalAttempts = 2;

TEST_F(DevicePortalDetectionTest, NoSelectedService) {
  device_->set_selected_service_for_testing(nullptr);
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).Times(0);
  EXPECT_CALL(*service_, IsConnected(nullptr)).Times(0);
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);

  EXPECT_FALSE(UpdatePortalDetector(true));
  EXPECT_FALSE(UpdatePortalDetector(false));
  ASSERT_EQ(nullptr, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, NoConnection) {
  EXPECT_CALL(*network_, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).Times(0);
  EXPECT_CALL(*service_, IsConnected(nullptr)).Times(0);
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);

  EXPECT_FALSE(UpdatePortalDetector(true));
  EXPECT_FALSE(UpdatePortalDetector(false));
  ASSERT_EQ(nullptr, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionDisabled) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(2);

  EXPECT_FALSE(UpdatePortalDetector(true));
  EXPECT_FALSE(UpdatePortalDetector(false));
  ASSERT_EQ(nullptr, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionInProgress_DoNotForceRestart) {
  auto mock_portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*mock_portal_detector, IsInProgress())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);

  EXPECT_TRUE(UpdatePortalDetector(false));
  ASSERT_EQ(mock_portal_detector, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionInProgress_ForceRestart) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const ManagerProperties props = MakePortalProperties();
  const std::vector<std::string> kDNSServers;

  auto mock_portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*mock_portal_detector, IsInProgress())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(kDNSServers));

  EXPECT_TRUE(UpdatePortalDetector(true));
  ASSERT_NE(mock_portal_detector, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionBadUrl) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const ManagerProperties props;
  const std::vector<std::string> kDNSServers;

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(kDNSServers));

  EXPECT_FALSE(UpdatePortalDetector());
  ASSERT_EQ(nullptr, GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionStart) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const auto props = MakePortalProperties();
  const std::vector<std::string> kDNSServers;

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(kDNSServers));
  EXPECT_TRUE(UpdatePortalDetector());
  ASSERT_NE(nullptr, GetPortalDetector());

  StopPortalDetection();
}

TEST_F(DevicePortalDetectionTest, PortalDetectionStartIPv6) {
  const IPAddress ip_addr = IPAddress("2001:db8:0:1::1");
  const auto props = MakePortalProperties();
  const std::vector<std::string> kDNSServers;

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(kDNSServers));
  EXPECT_TRUE(UpdatePortalDetector());
  ASSERT_NE(nullptr, GetPortalDetector());

  StopPortalDetection();
}

TEST_F(DevicePortalDetectionTest, PortalRetryAfterDetectionFailure) {
  const int kFailureStatusCode = 204;
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kConnection,
  result.http_status = PortalDetector::Status::kFailure;
  result.http_status_code = kFailureStatusCode;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseConnection,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*metrics(), SendEnumToUMA(_, Technology(Technology::kUnknown), _))
      .Times(AnyNumber());
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricPortalResult,
                            Technology(Technology::kUnknown),
                            Metrics::kPortalResultConnectionFailure));
  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline", _, _, _, _))
      .Times(0);
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillOnce(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillOnce(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionSuccess) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_, SetPortalDetectionFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_CALL(*metrics(), SendEnumToUMA(_, Technology(Technology::kUnknown), _))
      .Times(AnyNumber());
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricPortalResult,
                                        Technology(Technology::kUnknown),
                                        Metrics::kPortalResultSuccess));
  EXPECT_CALL(*metrics(),
              SendToUMA(Metrics::kMetricPortalAttemptsToOnline,
                        Technology(Technology::kUnknown), kPortalAttempts));
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent;
  result.http_status = PortalDetector::Status::kSuccess;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.num_attempts = kPortalAttempts;
  PortalDetectorCallback(result);
}

// The first portal detection attempt is inconclusive and the next portal
// detection attempt fails, causing the Service State to be set to 'online'
// since portal detection did not run.
TEST_F(DevicePortalDetectionTest, NextAttemptFails) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  // If portal detection attempts fail, default to optimistically assuming that
  // the Service is 'online'.
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_CALL(*service_, SetPortalDetectionFailure(
                             StrEq(kPortalDetectionPhaseDns),
                             StrEq(kPortalDetectionStatusTimeout), 0));
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillOnce(Return(attempt_delay));
  // The second portal detection attempt fails immediately, forcing the Device
  // to assume the Service state is 'online'.
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillOnce(Return(false));

  // First result indicating no connectivity and triggering a new portal
  // detection attempt.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kTimeout;
  result.https_phase = PortalDetector::Phase::kDNS;
  result.https_status = PortalDetector::Status::kTimeout;
  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, ScheduleNextDetectionAttempt) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  // If portal detection attempts run successfully and do not validate the
  // network, the Service state does not become 'online'.
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*service_, SetPortalDetectionFailure(
                             StrEq(kPortalDetectionPhaseDns),
                             StrEq(kPortalDetectionStatusTimeout), 0));
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillOnce(Return(attempt_delay));
  // The second portal detection attempt succeeds.
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillOnce(Return(true));

  // First result indicating no connectivity and triggering a new portal
  // detection attempt.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kTimeout;
  result.https_phase = PortalDetector::Phase::kDNS;
  result.https_status = PortalDetector::Status::kTimeout;
  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, RestartPortalDetection) {
  const IPAddress ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> kDNSServers = {"8.8.8.8", "8.8.4.4"};
  auto attempt_delay = base::Milliseconds(13450);
  const auto props = MakePortalProperties();

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(kDNSServers));
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  for (int i = 0; i < 10; i++) {
    EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
        .WillOnce(Return(attempt_delay));
    EXPECT_CALL(*portal_detector,
                Start(_, kDeviceName, ip_addr, kDNSServers, _, attempt_delay))
        .WillOnce(Return(true));
    EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
    EXPECT_CALL(*service_, SetPortalDetectionFailure(
                               StrEq(kPortalDetectionPhaseDns),
                               StrEq(kPortalDetectionStatusTimeout), 0));

    PortalDetector::Result result;
    result.http_phase = PortalDetector::Phase::kDNS,
    result.http_status = PortalDetector::Status::kTimeout;
    result.https_phase = PortalDetector::Phase::kDNS;
    result.https_status = PortalDetector::Status::kTimeout;
    PortalDetectorCallback(result);

    attempt_delay += base::Milliseconds(5678);
  }
}

TEST_F(DevicePortalDetectionTest, CancelledOnSelectService) {
  SetMockPortalDetector();
  EXPECT_CALL(*service_, state()).WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service_, SetState(_));
  EXPECT_CALL(*service_, SetAttachedNetwork(IsWeakPtrTo(nullptr)));
  SelectService(nullptr);
  EXPECT_FALSE(GetPortalDetector());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionDNSFailure) {
  const int kFailureStatusCode = 204;

  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.http_status_code = kFailureStatusCode;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseDns,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionDNSTimeout) {
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kTimeout;
  result.http_status_code = 0;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseDns,
                                        kPortalDetectionStatusTimeout, 0));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionRedirect) {
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.http_status_code = 302;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.redirect_url_string = props.portal_http_url;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusRedirect, 302));
  EXPECT_CALL(*service_, SetState(Service::kStateRedirectFound));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection())
      .Times(0);
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionRedirectNoUrl) {
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.http_status_code = 302;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusRedirect, 302));
  EXPECT_CALL(*service_, SetState(Service::kStatePortalSuspected));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionPortalSuspected) {
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.http_status_code = 204;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusSuccess, 204));
  EXPECT_CALL(*service_, SetState(Service::kStatePortalSuspected));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionNoConnectivity) {
  const auto ip_addr = IPAddress("1.2.3.4");
  const std::vector<std::string> dns_list = {"8.8.8.8", "8.8.4.4"};
  const auto props = MakePortalProperties();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kUnknown,
  result.http_status = PortalDetector::Status::kFailure;
  result.http_status_code = 0;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kPortalAttempts;
  const auto attempt_delay = base::Milliseconds(13450);

  EXPECT_CALL(manager_, GetProperties()).WillRepeatedly(ReturnRef(props));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseUnknown,
                                        kPortalDetectionStatusFailure, 0));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));

  EXPECT_CALL(*network_, local()).WillRepeatedly(Return(ip_addr));
  EXPECT_CALL(*network_, dns_servers()).WillRepeatedly(Return(dns_list));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection());
  MockPortalDetector* portal_detector = SetMockPortalDetector();
  EXPECT_CALL(*portal_detector, GetNextAttemptDelay())
      .WillRepeatedly(Return(attempt_delay));
  EXPECT_CALL(*portal_detector,
              Start(_, kDeviceName, ip_addr, dns_list, _, attempt_delay))
      .WillRepeatedly(Return(true));

  PortalDetectorCallback(result);
}

}  // namespace shill
