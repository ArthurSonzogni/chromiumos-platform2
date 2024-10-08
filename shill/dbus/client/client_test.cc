// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/client/client.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-proxy-mocks.h>

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;

using org::chromium::flimflam::DeviceProxyInterface;
using org::chromium::flimflam::DeviceProxyMock;
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ManagerProxyMock;
using org::chromium::flimflam::ServiceProxyInterface;
using org::chromium::flimflam::ServiceProxyMock;

namespace shill {
namespace {

class FakeClient : public Client {
 public:
  explicit FakeClient(scoped_refptr<dbus::Bus> bus)
      : Client(bus), manager_mock_(new ManagerProxyMock) {
    manager_proxy_.reset(manager_mock_);
  }
  virtual ~FakeClient() = default;

  void NotifyOwnerChange(const std::string& old_owner,
                         const std::string& new_owner) {
    OnOwnerChange(old_owner, new_owner);
  }

  void NotifyManagerPropertyChange(const std::string& name,
                                   const brillo::Any& value) {
    OnManagerPropertyChange(name, value);
  }

  void NotifyDevicePropertyChange(const std::string& path,
                                  const std::string& name,
                                  const brillo::Any& value) {
    OnDevicePropertyChange(path, name, value);
  }

  void NotifyServicePropertyChange(const std::string& device_path,
                                   const std::string& property_name,
                                   const brillo::Any& property_value) {
    OnServicePropertyChange(device_path, property_name, property_value);
  }

  ManagerProxyMock* manager() { return manager_mock_; }

  DeviceProxyMock* PreMakeDevice(const dbus::ObjectPath& device_path) {
    auto* mock = new DeviceProxyMock();
    device_mocks_[device_path.value()] = mock;
    // We need to keep these objects around all the way until the client is
    // destructed.
    static std::vector<std::unique_ptr<dbus::ObjectPath>> paths;
    auto path = std::make_unique<dbus::ObjectPath>(device_path.value());
    paths.emplace_back(std::move(path));
    EXPECT_CALL(*mock, GetObjectPath)
        .WillRepeatedly(ReturnRef(*paths.back().get()));
    return mock;
  }

  ServiceProxyMock* PreMakeService(const dbus::ObjectPath& service_path) {
    auto* mock = new ServiceProxyMock();
    service_mocks_[service_path.value()] = mock;
    // We need to keep these objects around all the way until the client is
    // destructed.
    static std::vector<std::unique_ptr<dbus::ObjectPath>> paths;
    auto path = std::make_unique<dbus::ObjectPath>(service_path.value());
    paths.emplace_back(std::move(path));
    EXPECT_CALL(*mock, GetObjectPath)
        .WillRepeatedly(ReturnRef(*paths.back().get()));
    return mock;
  }

 protected:
  // Pass back the pre-allocated device which is necessary for correctly setting
  // expectations in tests. Unfortunately this isn't going to let us re-add a
  // device with the same path.
  std::unique_ptr<DeviceProxyInterface> NewDeviceProxy(
      const dbus::ObjectPath& device_path) override {
    const auto it = device_mocks_.find(device_path.value());
    if (it == device_mocks_.end()) {
      return nullptr;
    }

    std::unique_ptr<DeviceProxyMock> mock;
    mock.reset(it->second);
    return mock;
  }

  std::unique_ptr<ServiceProxyInterface> NewServiceProxy(
      const dbus::ObjectPath& service_path) override {
    const auto it = service_mocks_.find(service_path.value());
    // Auto-premake for tests that don't need it.
    ServiceProxyMock* ptr = (it == service_mocks_.end())
                                ? PreMakeService(service_path)
                                : it->second;
    std::unique_ptr<ServiceProxyMock> mock;
    mock.reset(ptr);
    return mock;
  }

  ManagerProxyMock* manager_mock_;
  std::map<std::string, DeviceProxyMock*> device_mocks_;
  std::map<std::string, ServiceProxyMock*> service_mocks_;
};

class ClientTest : public testing::Test {
 protected:
  void SetUp() override {
    default_device_ = {};
    devices_.clear();
    last_device_changed_.clear();
    last_device_cxn_state_ = Client::Device::ConnectionState::kUnknown;

    // It's necessary to mock the base object used for the
    // SetNameOwnerChangedCallback.
    base_mock_ = new dbus::MockObjectProxy(
        bus_mock_.get(), kFlimflamServiceName, dbus::ObjectPath("/"));
    EXPECT_CALL(*bus_mock_,
                GetObjectProxy(kFlimflamServiceName, dbus::ObjectPath("/")))
        .WillRepeatedly(Return(base_mock_.get()));
    EXPECT_CALL(*base_mock_, SetNameOwnerChangedCallback(_));

    client_ = std::make_unique<FakeClient>(bus_mock_);
    client_->RegisterDefaultDeviceChangedHandler(base::BindRepeating(
        &ClientTest::DefaultDeviceHandler, base::Unretained(this)));
    client_->RegisterDeviceAddedHandler(base::BindRepeating(
        &ClientTest::DeviceAddedHandler, base::Unretained(this)));
    client_->RegisterDeviceRemovedHandler(base::BindRepeating(
        &ClientTest::DeviceRemovedHandler, base::Unretained(this)));
    client_->RegisterDeviceChangedHandler(base::BindRepeating(
        &ClientTest::DeviceChangedHandler, base::Unretained(this)));
  }

  void TearDown() override { client_.reset(); }

  void DeviceAddedHandler(const Client::Device* const device) {
    ASSERT_TRUE(device);
    EXPECT_TRUE(devices_.find(device->ifname) == devices_.end());
    devices_.emplace(device->ifname, *device);
  }
  void DeviceRemovedHandler(const Client::Device* const device) {
    ASSERT_TRUE(device);
    EXPECT_TRUE(devices_.find(device->ifname) != devices_.end());
    devices_.erase(device->ifname);
  }
  void DefaultDeviceHandler(const Client::Device* const device) {
    if (!device) {
      default_device_ = {};
      return;
    }
    default_device_ = *device;
  }
  void DeviceChangedHandler(const Client::Device* const device) {
    ASSERT_TRUE(device);
    last_device_changed_ = device->ifname;
    last_device_cxn_state_ = device->state;
  }

  scoped_refptr<dbus::MockBus> bus_mock_{
      new dbus::MockBus{dbus::Bus::Options{}}};
  scoped_refptr<dbus::MockObjectProxy> base_mock_;
  std::unique_ptr<FakeClient> client_;

  Client::Device default_device_;
  std::map<std::string, Client::Device> devices_;
  std::string last_device_changed_;
  Client::Device::ConnectionState last_device_cxn_state_;
};

ACTION_TEMPLATE(MovePointee,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(pointer)) {
  *pointer = std::move(*(::std::get<k>(args)));
}

TEST_F(ClientTest, DefaultServiceChangeCallsDefaultHandler) {
  // We want the device to exist first so the client doesn't run through the new
  // device setup process when it detects the change.
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback device_callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&device_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  props[kInterfaceProperty] = std::string("eth0");
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(device_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  EXPECT_EQ(default_device_.ifname, "");

  // Set the default service.
  const dbus::ObjectPath service_path("/service/1");
  auto* mock_default_service = client_->PreMakeService(service_path);
  brillo::VariantDictionary service_props;
  service_props[kDeviceProperty] = device_path;
  EXPECT_CALL(*mock_default_service, GetProperties)
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));

  client_->NotifyManagerPropertyChange(kDefaultServiceProperty, service_path);

  EXPECT_EQ(default_device_.ifname, "eth0");
}

TEST_F(ClientTest, DefaultServiceDisconnectedCallsDefaultHandler) {
  // We want the device to exist first so the client doesn't run through the new
  // device setup process when it detects the change.
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback device_callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&device_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  props[kInterfaceProperty] = std::string("eth0");
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(device_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  // Set the default service.
  const dbus::ObjectPath service_path("/service/1");
  auto* mock_default_service = client_->PreMakeService(service_path);
  brillo::VariantDictionary service_props;
  service_props[kDeviceProperty] = device_path;
  EXPECT_CALL(*mock_default_service, GetProperties)
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));

  client_->NotifyManagerPropertyChange(kDefaultServiceProperty, service_path);

  EXPECT_EQ(default_device_.ifname, "eth0");

  // Service disconnection should trigger the handler.
  client_->NotifyManagerPropertyChange(kDefaultServiceProperty,
                                       dbus::ObjectPath("/"));
  EXPECT_EQ(default_device_.ifname, "");
}

TEST_F(ClientTest, DefaultServiceChangeAddsDefaultDeviceIfMissing) {
  // Normally, all physical devices are already known so when one becomes the
  // default it will only trigger the default device callback. But VPN devices
  // are (intentionally) not provided by shill and are therefore not tracked.
  // But this client will add VPN devices if they become the device associated
  // with the default service.
  // Note that this also verifies that if for some reason (odd-ball
  // synchronization) when the default device is discover, if it happens be to
  // unknown at the time, it will be added and tracked.
  const dbus::ObjectPath device_path("/device/ppp0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback device_callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&device_callback));

  brillo::VariantDictionary device_props;
  device_props[kTypeProperty] = std::string(kTypePPP);
  device_props[kInterfaceProperty] = std::string("ppp0");
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(device_props), Return(true)));

  const dbus::ObjectPath service_path("/service/vpn");
  auto* mock_default_service = client_->PreMakeService(service_path);
  brillo::VariantDictionary service_props;
  service_props[kDeviceProperty] = device_path;
  EXPECT_CALL(*mock_default_service, GetProperties)
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));

  // Trigger the state change.
  client_->NotifyManagerPropertyChange(kDefaultServiceProperty, service_path);

  std::move(device_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  EXPECT_EQ(devices_.size(), 1);
  EXPECT_TRUE(devices_.find("ppp0") != devices_.end());

  // This change should have also invoked the default device handler as well.
  EXPECT_EQ(default_device_.ifname, "ppp0");
}

TEST_F(ClientTest, DeviceAddedHandlerCalledOncePerNewDevice) {
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  props[kInterfaceProperty] = std::string("eth0");
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(callback).Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  EXPECT_EQ(devices_.size(), 1);
  EXPECT_TRUE(devices_.find("eth0") != devices_.end());

  // Attempt to add the same device, this should not trigger the creation of a
  // new proxy or property registration and the rest. If this logic fails and
  // the device is readded, the above expectation(s) will be oversaturated and
  // fail.
  devices_.clear();
  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));
  EXPECT_TRUE(devices_.empty());
}

TEST_F(ClientTest, DeviceAddedHandlerNotCalledIfInterfaceMissing) {
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(callback).Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  EXPECT_TRUE(devices_.empty());
}

TEST_F(ClientTest, DeviceRemovedHandlerCalled) {
  // Add the device.
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  props[kInterfaceProperty] = std::string("eth0");
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(callback).Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  EXPECT_EQ(devices_.size(), 1);
  EXPECT_TRUE(devices_.find("eth0") != devices_.end());

  // Remove the device by updating the device list.
  client_->NotifyManagerPropertyChange(kDevicesProperty,
                                       std::vector<dbus::ObjectPath>({}));
  EXPECT_TRUE(devices_.empty());
}

TEST_F(ClientTest, DeviceWithoutInterfaceName) {
  // Add the device.
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler)
      .WillOnce(MovePointee<1>(&callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  // No interface name at the beginning.
  brillo::VariantDictionary props;
  props[kTypeProperty] = std::string(kTypeEthernet);
  props[kInterfaceProperty] = std::string("");
  EXPECT_CALL(*mock_device, GetProperties)
      .WillOnce(DoAll(testing::SetArgPointee<0>(props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(callback).Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  // The callback won't be triggered since there is no interface name.
  ASSERT_EQ(devices_.size(), 0);

  // Add the interface name. DeviceAdded callback should be triggered.
  client_->NotifyDevicePropertyChange(device_path.value(), kInterfaceProperty,
                                      std::string("eth0"));
  ASSERT_EQ(devices_.size(), 1);
  EXPECT_TRUE(base::Contains(devices_, "eth0"));

  // Remove the interface name. DeviceRemoved callback should be triggered.
  client_->NotifyDevicePropertyChange(device_path.value(), kInterfaceProperty,
                                      std::string(""));
  ASSERT_EQ(devices_.size(), 0);

  // Add the interface name again. DeviceAdded callback should be triggered.
  client_->NotifyDevicePropertyChange(device_path.value(), kInterfaceProperty,
                                      std::string("eth1"));
  ASSERT_EQ(devices_.size(), 1);
  EXPECT_TRUE(base::Contains(devices_, "eth1"));

  // Remove the interface name. DeviceRemoved callback should be triggered.
  client_->NotifyDevicePropertyChange(device_path.value(), kInterfaceProperty,
                                      std::string(""));
  ASSERT_EQ(devices_.size(), 0);

  // Remove the device by updating the device list. No callback should be
  // triggered (this is checked in `ClientTest::DeviceRemovedHandler()`).
  client_->NotifyManagerPropertyChange(kDevicesProperty,
                                       std::vector<dbus::ObjectPath>({}));
}

TEST_F(ClientTest, ParseNetworkConfig) {
  const dbus::ObjectPath device_path("/device/eth0");
  auto* mock_device = client_->PreMakeDevice(device_path);
  dbus::ObjectProxy::OnConnectedCallback callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler)
      .WillOnce(MovePointee<1>(&callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary device_props;
  device_props[kTypeProperty] = std::string(kTypeEthernet);
  device_props[kInterfaceProperty] = std::string("eth0");
  EXPECT_CALL(*mock_device, GetProperties)
      .WillOnce(DoAll(testing::SetArgPointee<0>(device_props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(callback).Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  brillo::VariantDictionary network_config_props;
  network_config_props[kNetworkConfigIPv4AddressProperty] =
      std::string("1.2.3.4/24");
  network_config_props[kNetworkConfigIPv4GatewayProperty] =
      std::string("1.2.3.5");
  network_config_props[kNetworkConfigIPv6AddressesProperty] =
      std::vector<std::string>{"fd00::1/64", "fd00::2/64"};
  network_config_props[kNetworkConfigIPv6GatewayProperty] =
      std::string("fd00::3");
  network_config_props[kNetworkConfigNameServersProperty] =
      std::vector<std::string>{
          "8.8.8.8", "fd00::4",
          "0.0.0.0",  // Empty address should be ignored.
      };
  network_config_props[kNetworkConfigSearchDomainsProperty] =
      std::vector<std::string>{"domain1", "domain2"};

  client_->NotifyServicePropertyChange(
      device_path.value(), shill::kNetworkConfigProperty, network_config_props);

  const auto devices = client_->GetDevices();
  ASSERT_EQ(devices.size(), 1);
  const Client::Device* d = devices[0].get();

  EXPECT_EQ(d->network_config.ipv4_address,
            net_base::IPv4CIDR::CreateFromCIDRString("1.2.3.4/24"));
  EXPECT_EQ(d->network_config.ipv4_gateway,
            net_base::IPv4Address::CreateFromString("1.2.3.5"));
  EXPECT_EQ(d->network_config.ipv6_addresses,
            std::vector<net_base::IPv6CIDR>(
                {*net_base::IPv6CIDR::CreateFromCIDRString("fd00::1/64"),
                 *net_base::IPv6CIDR::CreateFromCIDRString("fd00::2/64")}));
  EXPECT_EQ(d->network_config.ipv6_gateway,
            net_base::IPv6Address::CreateFromString("fd00::3"));
  EXPECT_EQ(d->network_config.dns_servers,
            std::vector<net_base::IPAddress>(
                {*net_base::IPAddress::CreateFromString("8.8.8.8"),
                 *net_base::IPAddress::CreateFromString("fd00::4")}));
  EXPECT_EQ(d->network_config.dns_search_domains,
            std::vector<std::string>({"domain1", "domain2"}));
}

TEST_F(ClientTest, DeviceHandlersCalledOnNetworkConfigChange) {
  // Set up 2 devices here to ensure the changes are captured correctly.
  const dbus::ObjectPath eth0_path("/device/eth0"), wlan0_path("/device/wlan0"),
      eth0_service_path("service/0"), wlan0_service_path("/service/1");
  auto* eth0_device = client_->PreMakeDevice(eth0_path);
  auto* wlan0_device = client_->PreMakeDevice(wlan0_path);
  dbus::ObjectProxy::OnConnectedCallback eth0_callback, wlan0_callback;
  EXPECT_CALL(*eth0_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&eth0_callback));
  EXPECT_CALL(*wlan0_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&wlan0_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({eth0_path, wlan0_path}));

  brillo::VariantDictionary eth0_props, wlan0_props;
  eth0_props[kTypeProperty] = std::string(kTypeEthernet);
  eth0_props[kInterfaceProperty] = std::string("eth0");
  eth0_props[kSelectedServiceProperty] = eth0_service_path;
  EXPECT_CALL(*eth0_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(eth0_props), Return(true)));
  wlan0_props[kTypeProperty] = std::string(kTypeWifi);
  wlan0_props[kInterfaceProperty] = std::string("wlan0");
  wlan0_props[kSelectedServiceProperty] = wlan0_service_path;
  EXPECT_CALL(*wlan0_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(wlan0_props), Return(true)));

  std::move(eth0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(wlan0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  // Prepare two different NetworkConfig values.
  brillo::VariantDictionary props1 = {
      {kNetworkConfigIPv4AddressProperty, std::string("1.2.3.4/24")}};
  brillo::VariantDictionary props2 = {
      {kNetworkConfigIPv4AddressProperty, std::string("1.2.3.5/24")}};

  // Value is changed for eth0.
  client_->NotifyServicePropertyChange(eth0_path.value(),
                                       kNetworkConfigProperty, props1);
  EXPECT_EQ(last_device_changed_, "eth0");

  // Value is changed for wlan0.
  client_->NotifyServicePropertyChange(wlan0_path.value(),
                                       kNetworkConfigProperty, props1);
  EXPECT_EQ(last_device_changed_, "wlan0");

  // Value is NOT changed for eth0.
  client_->NotifyServicePropertyChange(eth0_path.value(),
                                       kNetworkConfigProperty, props1);
  EXPECT_EQ(last_device_changed_, "wlan0");

  // Value is changed for eth0.
  client_->NotifyServicePropertyChange(eth0_path.value(),
                                       kNetworkConfigProperty, props2);
  EXPECT_EQ(last_device_changed_, "eth0");
}

TEST_F(ClientTest, DeviceSelectedServiceConnectStateObtained) {
  const dbus::ObjectPath device_path("/device/eth0"),
      service_path("/service/1");
  auto* mock_device = client_->PreMakeDevice(device_path);
  auto* mock_service = client_->PreMakeService(service_path);
  dbus::ObjectProxy::OnConnectedCallback device_callback, service_callback;
  EXPECT_CALL(*mock_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&device_callback));
  EXPECT_CALL(*mock_service, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&service_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({device_path}));

  brillo::VariantDictionary device_props;
  device_props[kTypeProperty] = std::string(kTypeEthernet);
  device_props[kInterfaceProperty] = std::string("eth0");
  device_props[kSelectedServiceProperty] = service_path;
  EXPECT_CALL(*mock_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(device_props), Return(true)));

  brillo::VariantDictionary service_props;
  service_props[kStateProperty] = std::string(kStateOnline);
  EXPECT_CALL(*mock_service, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));

  // Manually trigger the registration callback since that doesn't happen in the
  // mocks.
  std::move(device_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(service_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  EXPECT_EQ(devices_.size(), 1);
  const auto it = devices_.find("eth0");
  EXPECT_NE(it, devices_.end());
  EXPECT_EQ(it->second.state, Client::Device::ConnectionState::kOnline);
}

TEST_F(ClientTest, DeviceHandlersCalledOnSelectedServiceStateChange) {
  // Set up 2 devices here to ensure the changes are captured correctly.
  const dbus::ObjectPath eth0_path("/device/eth0"), wlan0_path("/device/wlan0"),
      eth0_service_path("/service/0"), wlan0_service_path("/service/1");
  auto* eth0_device = client_->PreMakeDevice(eth0_path);
  auto* wlan0_device = client_->PreMakeDevice(wlan0_path);
  auto* eth0_service = client_->PreMakeService(eth0_service_path);
  auto* wlan0_service = client_->PreMakeService(wlan0_service_path);
  dbus::ObjectProxy::OnConnectedCallback eth0_callback, wlan0_callback,
      eth0_service_callback, wlan0_service_callback;
  EXPECT_CALL(*eth0_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&eth0_callback));
  EXPECT_CALL(*wlan0_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&wlan0_callback));
  EXPECT_CALL(*eth0_service, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&eth0_service_callback));
  EXPECT_CALL(*wlan0_service, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&wlan0_service_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({eth0_path, wlan0_path}));

  brillo::VariantDictionary eth0_props, wlan0_props;
  eth0_props[kTypeProperty] = std::string(kTypeEthernet);
  eth0_props[kInterfaceProperty] = std::string("eth0");
  eth0_props[kSelectedServiceProperty] = eth0_service_path;
  EXPECT_CALL(*eth0_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(eth0_props), Return(true)));
  wlan0_props[kTypeProperty] = std::string(kTypeWifi);
  wlan0_props[kInterfaceProperty] = std::string("wlan0");
  wlan0_props[kSelectedServiceProperty] = wlan0_service_path;
  EXPECT_CALL(*wlan0_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(wlan0_props), Return(true)));

  brillo::VariantDictionary eth0_service_props, wlan0_service_props;
  eth0_service_props[kStateProperty] = std::string(kStateOnline);
  eth0_service_props[kDeviceProperty] = dbus::ObjectPath(eth0_path);
  wlan0_service_props[kStateProperty] = std::string(kStateOnline);
  wlan0_service_props[kDeviceProperty] = dbus::ObjectPath(wlan0_path);

  EXPECT_CALL(*eth0_service, GetProperties)
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(eth0_service_props), Return(true)));
  EXPECT_CALL(*wlan0_service, GetProperties)
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(wlan0_service_props), Return(true)));

  // We don't set default service and do the related check here, since it's
  // would be hard to do that under the current architecture.

  std::move(eth0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(wlan0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(eth0_service_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(wlan0_service_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  last_device_changed_.clear();
  client_->NotifyServicePropertyChange("/device/wlan0", kStateProperty,
                                       std::string(kStateFailure));
  EXPECT_EQ(last_device_changed_, "wlan0");
  EXPECT_EQ(last_device_cxn_state_, Client::Device::ConnectionState::kFailure);

  client_->NotifyServicePropertyChange("/device/eth0", kStateProperty,
                                       std::string(kStateReady));
  EXPECT_EQ(last_device_changed_, "eth0");
  EXPECT_EQ(last_device_cxn_state_, Client::Device::ConnectionState::kReady);
}

TEST_F(ClientTest, DeviceHandlersCalledOnSelectedServiceChange) {
  const dbus::ObjectPath wlan0_path("/device/wlan0"),
      service0_path("/service/0"), service1_path("/service/1");
  auto* wlan0_device = client_->PreMakeDevice(wlan0_path);
  auto* service0 = client_->PreMakeService(service0_path);
  auto* service1 = client_->PreMakeService(service1_path);
  dbus::ObjectProxy::OnConnectedCallback wlan0_callback, service0_callback,
      service1_callback;
  EXPECT_CALL(*wlan0_device, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&wlan0_callback));
  EXPECT_CALL(*service0, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&service0_callback));
  EXPECT_CALL(*service1, DoRegisterPropertyChangedSignalHandler(_, _))
      .WillOnce(MovePointee<1>(&service1_callback));

  client_->NotifyManagerPropertyChange(
      kDevicesProperty, std::vector<dbus::ObjectPath>({wlan0_path}));

  brillo::VariantDictionary wlan0_props;
  wlan0_props[kTypeProperty] = std::string(kTypeWifi);
  wlan0_props[kInterfaceProperty] = std::string("wlan0");
  wlan0_props[kSelectedServiceProperty] = service0_path;
  EXPECT_CALL(*wlan0_device, GetProperties(_, _, _))
      .WillOnce(DoAll(testing::SetArgPointee<0>(wlan0_props), Return(true)));

  brillo::VariantDictionary service_props;
  service_props[kStateProperty] = std::string(kStateOnline);
  EXPECT_CALL(*service0, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));
  EXPECT_CALL(*service1, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service_props), Return(true)));

  std::move(wlan0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);
  std::move(service0_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  last_device_changed_.clear();
  client_->NotifyDevicePropertyChange("/device/wlan0", kSelectedServiceProperty,
                                      service1_path);
  std::move(service1_callback)
      .Run(kFlimflamServiceName, kMonitorPropertyChanged, true);

  EXPECT_EQ(last_device_changed_, "wlan0");
}

TEST_F(ClientTest, ManagerPropertyAccessor) {
  brillo::Any bar("bar");
  EXPECT_CALL(*client_->manager(), SetProperty(StrEq("foo"), bar, _, -1));
  EXPECT_CALL(*client_->manager(), SetProperty(StrEq("foo"), bar, _, 10));
  EXPECT_CALL(*client_->manager(),
              SetPropertyAsync(StrEq("foo"), bar, _, _, -1));
  EXPECT_CALL(*client_->manager(),
              SetPropertyAsync(StrEq("foo"), bar, _, _, 10));
  EXPECT_CALL(*client_->manager(), DoRegisterPropertyChangedSignalHandler(_, _))
      .Times(2);

  auto props = client_->ManagerProperties();
  props->Set("foo", "bar", nullptr);
  props->Set("foo", "bar", base::DoNothing(), base::DoNothing());

  props = client_->ManagerProperties(base::Milliseconds(10));
  props->Set("foo", "bar", nullptr);
  props->Set("foo", "bar", base::DoNothing(), base::DoNothing());
}

TEST_F(ClientTest, DefaultDeviceReturnsCorrectDeviceForVPN) {
  dbus::ObjectPath service0_path("/service/0"), service1_path("/service/1"),
      device0_path("/dev/0"), device1_path("/dev/1");
  brillo::VariantDictionary mgr_props;
  mgr_props[kServicesProperty] =
      std::vector<dbus::ObjectPath>({service0_path, service1_path});
  EXPECT_CALL(*client_->manager(), GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(mgr_props), Return(true)));

  auto* service0 = client_->PreMakeService(service0_path);
  brillo::VariantDictionary service0_props;
  service0_props[kTypeProperty] = std::string(kTypeVPN);
  service0_props[kStateProperty] = std::string(kStateOnline);
  service0_props[kDeviceProperty] = device0_path;
  EXPECT_CALL(*service0, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service0_props), Return(true)));

  auto* device0 = client_->PreMakeDevice(device0_path);
  brillo::VariantDictionary device0_props;
  device0_props[kTypeProperty] = std::string(kTypePPP);
  device0_props[kInterfaceProperty] = std::string("ppp0");
  EXPECT_CALL(*device0, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(device0_props), Return(true)));

  auto dev = client_->DefaultDevice(false /*exclude_vpn*/);
  EXPECT_TRUE(dev);
  EXPECT_EQ(dev->ifname, "ppp0");
  EXPECT_EQ(dev->type, Client::Device::Type::kPPP);
  EXPECT_EQ(dev->state, Client::Device::ConnectionState::kOnline);
}

TEST_F(ClientTest, DefaultDeviceReturnsCorrectDeviceExcludingVPN) {
  dbus::ObjectPath service0_path("/service/0"), service1_path("/service/1"),
      device0_path("/dev/0"), device1_path("/dev/1");
  brillo::VariantDictionary mgr_props;
  mgr_props[kServicesProperty] =
      std::vector<dbus::ObjectPath>({service0_path, service1_path});
  EXPECT_CALL(*client_->manager(), GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(mgr_props), Return(true)));

  auto* service0 = client_->PreMakeService(service0_path);
  brillo::VariantDictionary service0_props;
  service0_props[kTypeProperty] = std::string(kTypeVPN);
  service0_props[kStateProperty] = std::string(kStateOnline);
  service0_props[kDeviceProperty] = device0_path;
  EXPECT_CALL(*service0, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service0_props), Return(true)));

  auto* service1 = client_->PreMakeService(service1_path);
  brillo::VariantDictionary service1_props;
  service1_props[kTypeProperty] = std::string(kTypeWifi);
  service1_props[kStateProperty] = std::string(kStateOnline);
  service1_props[kDeviceProperty] = device1_path;
  EXPECT_CALL(*service1, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(service1_props), Return(true)));

  auto* device1 = client_->PreMakeDevice(device1_path);
  brillo::VariantDictionary device1_props;
  device1_props[kTypeProperty] = std::string(kTypeWifi);
  device1_props[kInterfaceProperty] = std::string("wlan0");
  EXPECT_CALL(*device1, GetProperties(_, _, _))
      .WillRepeatedly(
          DoAll(testing::SetArgPointee<0>(device1_props), Return(true)));

  auto dev = client_->DefaultDevice(true /*exclude_vpn*/);
  EXPECT_TRUE(dev);
  EXPECT_EQ(dev->ifname, "wlan0");
  EXPECT_EQ(dev->type, Client::Device::Type::kWifi);
  EXPECT_EQ(dev->state, Client::Device::ConnectionState::kOnline);
}

}  // namespace
}  // namespace shill
