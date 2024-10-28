// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_config.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/mock_virtual_device.h"
#include "shill/network/network.h"
#include "shill/service_property_change_test.h"
#include "shill/store/fake_store.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/mock_vpn_metrics.h"
#include "shill/vpn/mock_vpn_provider.h"
#include "shill/vpn/vpn_end_reason.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_types.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SaveArg;

namespace {

constexpr char kInterfaceName[] = "tun0";
constexpr int kInterfaceIndex = 123;

}  // namespace

namespace shill {

class VPNServiceInTest : public VPNService {
 public:
  VPNServiceInTest(Manager* manager, VPNDriver* driver)
      : VPNService(manager, base::WrapUnique(driver)) {}

  bool CreateDevice(const std::string& if_name, int if_index) override {
    auto* mock_device = new MockVirtualDevice(this->manager(), if_name,
                                              if_index, Technology::kVPN);
    device_ = mock_device;

    // Use the default implementation for SelectService(), so that the Network
    // object will be attached to this service.
    ON_CALL(*mock_device, SelectService)
        .WillByDefault([mock_device](const ServiceRefPtr& service) {
          mock_device->VirtualDevice::SelectService(service);
        });

    return true;
  }

  // There can be a reference cycle between Device and Service. Call this
  // function to break it.
  void DestroyDevice() { device_ = nullptr; }
};

class VPNServiceTest : public testing::Test {
 public:
  VPNServiceTest() : manager_(&control_, &dispatcher_, &metrics_) {
    Service::SetNextSerialNumberForTesting(0);
    driver_ = new MockVPNDriver(&manager_, VPNType::kOpenVPN);
    driver_metrics_ = new MockVPNDriverMetrics();
    driver_->set_driver_metrics_for_testing(base::WrapUnique(driver_metrics_));
    service_ = new VPNServiceInTest(&manager_, driver_);
  }

  ~VPNServiceTest() override { service_->DestroyDevice(); }

 protected:
  void SetUp() override {
    manager_.vpn_provider_ = std::make_unique<MockVPNProvider>();
    manager_.vpn_provider_->manager_ = &manager_;
    manager_.UpdateProviderMapping();
  }

  void TearDown() override { manager_.vpn_provider_.reset(); }

  void SetServiceState(Service::ConnectState state) {
    service_->state_ = state;
  }

  void SetHasEverConnected(bool connected) {
    service_->has_ever_connected_ = connected;
  }

  void SetConnectable(bool connectable) {
    service_->connectable_ = connectable;
  }

  const char* GetAutoConnOffline() { return Service::kAutoConnOffline; }

  const char* GetAutoConnNeverConnected() {
    return VPNService::kAutoConnNeverConnected;
  }

  const char* GetAutoConnVPNAlreadyActive() {
    return VPNService::kAutoConnVPNAlreadyActive;
  }

  bool IsAutoConnectable(const char** reason) const {
    return service_->IsAutoConnectable(reason);
  }

  // Takes ownership of |provider|.
  void SetVPNProvider(VPNProvider* provider) {
    manager_.vpn_provider_.reset(provider);
    manager_.UpdateProviderMapping();
  }

  ServiceMockAdaptor* GetAdaptor() {
    return static_cast<ServiceMockAdaptor*>(service_->adaptor());
  }

  MockVPNDriverMetrics* driver_metrics_;  // Owned by |driver_|.
  MockVPNDriver* driver_;                 // Owned by |service_|.
  MockControl control_;
  MockMetrics metrics_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  scoped_refptr<VPNServiceInTest> service_;
};

TEST_F(VPNServiceTest, LogName) {
  EXPECT_EQ("vpn_openvpn_0", service_->log_name());
}

TEST_F(VPNServiceTest, ConnectAlreadyConnected) {
  EXPECT_TRUE(service_->connectable());

  Error error;
  EXPECT_CALL(*driver_, ConnectAsync(_)).Times(0);
  SetServiceState(Service::kStateOnline);
  service_->Connect(&error, "in test");
  EXPECT_EQ(Error::kAlreadyConnected, error.type());
  error.Reset();
  SetServiceState(Service::kStateConfiguring);
  service_->Connect(&error, "in test");
  EXPECT_EQ(Error::kInProgress, error.type());
}

TEST_F(VPNServiceTest, Disconnect) {
  Error error;
  service_->SetState(Service::kStateConnected);
  EXPECT_CALL(*driver_, Disconnect());
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoHost) {
  KeyValueStore args;
  Error error;
  args.Set<std::string>(kNameProperty, "vpn-name");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kInvalidProperty, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoName) {
  KeyValueStore args;
  Error error;
  args.Set<std::string>(kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kInvalidProperty, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifier) {
  KeyValueStore args;
  Error error;
  args.Set<std::string>(kNameProperty, "vpn-name");
  args.Set<std::string>(kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("vpn_10_8_0_1_vpn_name",
            VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, GetStorageIdentifier) {
  EXPECT_EQ("", service_->GetStorageIdentifier());
  service_->set_storage_id("foo");
  EXPECT_EQ("foo", service_->GetStorageIdentifier());
}

TEST_F(VPNServiceTest, Load) {
  FakeStore storage;
  static constexpr char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  storage.SetString(kStorageID, Service::kStorageType, kTypeVPN);
  EXPECT_CALL(*driver_, Load(&storage, kStorageID)).WillOnce(Return(true));
  EXPECT_TRUE(service_->Load(&storage));
}

TEST_F(VPNServiceTest, Save) {
  FakeStore storage;
  static constexpr char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
  std::string type;
  EXPECT_TRUE(storage.GetString(kStorageID, Service::kStorageType, &type));
  EXPECT_EQ(type, kTypeVPN);
}

TEST_F(VPNServiceTest, SaveCredentials) {
  FakeStore storage;
  static constexpr char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  service_->set_save_credentials(true);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, true))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(VPNServiceTest, Unload) {
  service_->SetAutoConnect(true);
  service_->set_save_credentials(true);
  service_->SetState(Service::kStateConnected);
  EXPECT_CALL(*driver_, Disconnect());
  EXPECT_CALL(*driver_, UnloadCredentials());
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  provider->services_.push_back(service_);
  service_->Unload();
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(service_->save_credentials());
  EXPECT_TRUE(provider->services_.empty());
}

TEST_F(VPNServiceTest, InitPropertyStore) {
  EXPECT_CALL(*driver_, InitPropertyStore(service_->mutable_store()));
  service_->InitDriverPropertyStore();
}

TEST_F(VPNServiceTest, EnableAndRetainAutoConnect) {
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());
  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());
}

TEST_F(VPNServiceTest, IsAutoConnectableOffline) {
  EXPECT_TRUE(service_->connectable());
  const char* reason = nullptr;
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(false));
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnOffline(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableNeverConnected) {
  EXPECT_TRUE(service_->connectable());
  EXPECT_FALSE(service_->has_ever_connected());
  const char* reason = nullptr;
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnNeverConnected(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableVPNAlreadyActive) {
  EXPECT_TRUE(service_->connectable());
  SetHasEverConnected(true);
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  EXPECT_CALL(*provider, HasActiveService()).WillOnce(Return(true));
  const char* reason = nullptr;
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnVPNAlreadyActive(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableNotConnectable) {
  const char* reason = nullptr;
  SetConnectable(false);
  EXPECT_FALSE(IsAutoConnectable(&reason));
}

TEST_F(VPNServiceTest, IsAutoConnectable) {
  EXPECT_TRUE(service_->connectable());
  SetHasEverConnected(true);
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  EXPECT_CALL(*provider, HasActiveService()).WillOnce(Return(false));
  const char* reason = nullptr;
  EXPECT_TRUE(IsAutoConnectable(&reason));
  EXPECT_FALSE(reason);
}

TEST_F(VPNServiceTest, SetNamePropertyTrivial) {
  Error error;
  // A null change returns false, but with error set to success.
  service_->mutable_store()->SetAnyProperty(
      kNameProperty, brillo::Any(service_->friendly_name()), &error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, SetNameProperty) {
  const std::string kHost = "1.2.3.4";
  driver_->args()->Set<std::string>(kProviderHostProperty, kHost);
  std::string kOldId = service_->GetStorageIdentifier();
  Error error;
  const std::string kName = "New Name";
  scoped_refptr<MockProfile> profile(new MockProfile(&manager_));
  EXPECT_CALL(*profile, DeleteEntry(kOldId, _));
  EXPECT_CALL(*profile, UpdateService(_));
  service_->set_profile(profile);
  service_->mutable_store()->SetAnyProperty(kNameProperty, brillo::Any(kName),
                                            &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_NE(service_->GetStorageIdentifier(), kOldId);
  EXPECT_EQ(kName, service_->friendly_name());
}

TEST_F(VPNServiceTest, PropertyChanges) {
  TestCommonPropertyChanges(service_, GetAdaptor());
  TestAutoConnectPropertyChange(service_, GetAdaptor());

  const std::string kHost = "1.2.3.4";
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(&manager_));
  service_->set_profile(profile);
  driver_->args()->Set<std::string>(kProviderHostProperty, kHost);
  TestNamePropertyChange(service_, GetAdaptor());
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(VPNServiceTest, CustomSetterNoopChange) {
  TestCustomSetterNoopChange(service_, &manager_);
}

TEST_F(VPNServiceTest, GetPhysicalTechnologyPropertyFailsIfNoCarrier) {
  service_->OnDefaultPhysicalServiceChanged(nullptr);

  Error error;
  EXPECT_EQ("", service_->GetPhysicalTechnologyProperty(&error));
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

TEST_F(VPNServiceTest, GetPhysicalTechnologyPropertyOverWifi) {
  scoped_refptr<MockService> underlying_service = new MockService(&manager_);
  EXPECT_CALL(*underlying_service, technology())
      .WillOnce(Return(Technology::kWiFi));
  service_->OnDefaultPhysicalServiceChanged(underlying_service);

  Error error;
  EXPECT_EQ(kTypeWifi, service_->GetPhysicalTechnologyProperty(&error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, GetTethering) {
  // Service is not connected.
  EXPECT_EQ(Service::TetheringState::kUnknown, service_->GetTethering());

  service_->SetState(Service::kStateConnected);
  service_->OnDefaultPhysicalServiceChanged(nullptr);
  EXPECT_EQ(Service::TetheringState::kUnknown, service_->GetTethering());

  scoped_refptr<MockService> underlying_service = new MockService(&manager_);
  EXPECT_CALL(*underlying_service, GetTethering())
      .WillOnce([]() { return Service::TetheringState::kNotDetected; })
      .WillOnce([]() { return Service::TetheringState::kUnknown; });
  service_->OnDefaultPhysicalServiceChanged(underlying_service);
  EXPECT_EQ(Service::TetheringState::kNotDetected, service_->GetTethering());
  EXPECT_EQ(Service::TetheringState::kUnknown, service_->GetTethering());
}

TEST_F(VPNServiceTest, ConfigureDeviceAndCleanupDevice) {
  scoped_refptr<MockVirtualDevice> device = new MockVirtualDevice(
      &manager_, kInterfaceName, kInterfaceIndex, Technology::kVPN);
  service_->device_ = device;

  EXPECT_CALL(*device, SetEnabled(true));
  EXPECT_CALL(*device, UpdateNetworkConfig(_));
  service_->ConfigureDevice(std::make_unique<net_base::NetworkConfig>());

  EXPECT_CALL(*device, SetEnabled(false));
  EXPECT_CALL(*device, DropConnection());
  service_->CleanupDevice();
  EXPECT_FALSE(service_->device_);
}

TEST_F(VPNServiceTest, ReportMetrics) {
  auto config = std::make_unique<net_base::NetworkConfig>();
  EXPECT_CALL(*driver_, GetNetworkConfig).WillOnce(Return(std::move(config)));
  EXPECT_CALL(*driver_metrics_, ReportConnected);
  EXPECT_CALL(*driver_metrics_, ReportNetworkConfig);
  service_->OnDriverConnected(kInterfaceName, kInterfaceIndex);
}

TEST_F(VPNServiceTest, ConnectFlow) {
  Error error;
  VPNDriver::EventHandler* driver_event_handler;

  // Connection
  EXPECT_CALL(*driver_, ConnectAsync(_))
      .WillOnce(DoAll(SaveArg<0>(&driver_event_handler),
                      Return(VPNDriver::kTimeoutNone)));
  EXPECT_CALL(*driver_metrics_, ReportConnecting);
  service_->Connect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(Service::kStateAssociating, service_->state());
  Mock::VerifyAndClearExpectations(driver_metrics_);

  EXPECT_CALL(*driver_, GetNetworkConfig)
      .WillOnce(Return(std::make_unique<net_base::NetworkConfig>()));
  EXPECT_CALL(*driver_metrics_, ReportConnected);
  driver_event_handler->OnDriverConnected(kInterfaceName, kInterfaceIndex);
  EXPECT_TRUE(service_->device_);
  EXPECT_EQ(Service::kStateConfiguring, service_->state());
  Mock::VerifyAndClearExpectations(driver_metrics_);

  // Driver-originated reconnection
  EXPECT_CALL(*driver_, Disconnect()).Times(0);
  EXPECT_CALL(*driver_metrics_, ReportReconnecting);
  driver_event_handler->OnDriverReconnecting(VPNDriver::kTimeoutNone);
  EXPECT_EQ(Service::kStateAssociating, service_->state());
  EXPECT_TRUE(service_->device_);
  Mock::VerifyAndClearExpectations(driver_metrics_);

  // Driver-originated failure
  EXPECT_CALL(*driver_, Disconnect()).Times(0);
  EXPECT_CALL(*driver_metrics_,
              ReportDisconnected(VPNEndReason::kFailureUnknown));
  driver_event_handler->OnDriverFailure(VPNEndReason::kFailureUnknown,
                                        Service::kErrorDetailsNone);
  EXPECT_EQ(Service::kStateFailure, service_->state());
  EXPECT_FALSE(service_->device_);
  Mock::VerifyAndClearExpectations(driver_metrics_);

  // Connect again and disconnection
  EXPECT_CALL(*driver_, ConnectAsync(_));
  EXPECT_CALL(*driver_metrics_, ReportConnecting);
  service_->Connect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(Service::kStateAssociating, service_->state());
  EXPECT_CALL(*driver_, Disconnect());
  EXPECT_CALL(*driver_metrics_,
              ReportDisconnected(VPNEndReason::kDisconnectRequest));
  service_->Disconnect(&error, "in test");
  EXPECT_EQ(Service::kStateIdle, service_->state());
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_FALSE(service_->device_);
  Mock::VerifyAndClearExpectations(driver_metrics_);
}

TEST_F(VPNServiceTest, OnPhysicalDefaultServiceChanged) {
  ServiceRefPtr null_service;
  scoped_refptr<MockService> mock_service(new MockService(&manager_));
  scoped_refptr<MockService> mock_service2(new MockService(&manager_));

  // No service -> online
  EXPECT_CALL(*mock_service, IsOnline()).WillRepeatedly(Return(true));
  EXPECT_CALL(*driver_, OnDefaultPhysicalServiceEvent(
                            VPNDriver::DefaultPhysicalServiceEvent::kUp));
  service_->OnDefaultPhysicalServiceChanged(mock_service);

  // Online service -> another online service
  EXPECT_CALL(*mock_service2, IsOnline()).WillRepeatedly(Return(true));
  EXPECT_CALL(*driver_, OnDefaultPhysicalServiceEvent(
                            VPNDriver::DefaultPhysicalServiceEvent::kChanged));
  service_->OnDefaultPhysicalServiceChanged(mock_service2);

  // Online -> connected
  EXPECT_CALL(*mock_service2, IsOnline()).WillRepeatedly(Return(false));
  EXPECT_CALL(*driver_, OnDefaultPhysicalServiceEvent(
                            VPNDriver::DefaultPhysicalServiceEvent::kDown));
  service_->OnDefaultPhysicalServiceChanged(mock_service2);

  // Connected -> another online service
  EXPECT_CALL(*mock_service, IsOnline()).WillRepeatedly(Return(true));
  EXPECT_CALL(*driver_, OnDefaultPhysicalServiceEvent(
                            VPNDriver::DefaultPhysicalServiceEvent::kUp));
  service_->OnDefaultPhysicalServiceChanged(mock_service);

  // Online -> no service
  EXPECT_CALL(*driver_, OnDefaultPhysicalServiceEvent(
                            VPNDriver::DefaultPhysicalServiceEvent::kDown));
  service_->OnDefaultPhysicalServiceChanged(null_service);
}

TEST_F(VPNServiceTest, ConnectTimeout) {
  Error error;
  VPNDriver::EventHandler* driver_event_handler;
  constexpr base::TimeDelta kTestTimeout = base::Seconds(10);
  ON_CALL(*driver_, GetNetworkConfig).WillByDefault([]() {
    return std::make_unique<net_base::NetworkConfig>();
  });

  // Timeout triggered.
  EXPECT_CALL(*driver_, ConnectAsync(_))
      .WillRepeatedly(
          DoAll(SaveArg<0>(&driver_event_handler), Return(kTestTimeout)));
  service_->Connect(&error, "in test");
  EXPECT_CALL(*driver_, OnConnectTimeout());
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);

  // Timeout cancelled by connection success.
  service_->Connect(&error, "in test");
  EXPECT_CALL(*driver_, OnConnectTimeout()).Times(0);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout / 2);
  driver_event_handler->OnDriverConnected(kInterfaceName, kInterfaceIndex);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);
  service_->Disconnect(&error, "in test");

  // Timeout cancelled by connection failure.
  service_->Connect(&error, "in test");
  EXPECT_CALL(*driver_, OnConnectTimeout()).Times(0);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout / 2);
  driver_event_handler->OnDriverFailure(VPNEndReason::kFailureUnknown,
                                        Service::kErrorDetailsNone);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);

  // No timeout
  EXPECT_CALL(*driver_, ConnectAsync(_))
      .WillRepeatedly(DoAll(SaveArg<0>(&driver_event_handler),
                            Return(VPNDriver::kTimeoutNone)));
  service_->Connect(&error, "in test");
  EXPECT_CALL(*driver_, OnConnectTimeout()).Times(0);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);
}

TEST_F(VPNServiceTest, ReconnectTimeout) {
  Error error;
  VPNDriver::EventHandler* driver_event_handler;
  constexpr base::TimeDelta kTestTimeout = base::Seconds(10);
  ON_CALL(*driver_, GetNetworkConfig).WillByDefault([]() {
    return std::make_unique<net_base::NetworkConfig>();
  });
  EXPECT_CALL(*driver_, ConnectAsync(_))
      .WillRepeatedly(DoAll(SaveArg<0>(&driver_event_handler),
                            Return(VPNDriver::kTimeoutNone)));
  service_->Connect(&error, "in test");
  driver_event_handler->OnDriverConnected(kInterfaceName, kInterfaceIndex);

  EXPECT_CALL(*driver_, OnConnectTimeout()).Times(0);
  driver_event_handler->OnDriverReconnecting(kTestTimeout);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout / 2);

  // Timeout should be reset.
  driver_event_handler->OnDriverReconnecting(kTestTimeout);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout / 2);

  // Timeout should be cancelled.
  driver_event_handler->OnDriverReconnecting(VPNDriver::kTimeoutNone);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);

  // Timeout triggered.
  driver_event_handler->OnDriverReconnecting(kTestTimeout);
  EXPECT_CALL(*driver_, OnConnectTimeout()).Times(1);
  dispatcher_.task_environment().FastForwardBy(kTestTimeout);
}

TEST_F(VPNServiceTest, MigrateWireGuardIPv4Address) {
  FakeStore storage;
  constexpr char kStorageID[] = "storage-id";
  const std::string kAddr1 = "1.2.3.4";
  const std::string kAddr2 = "4.3.2.1";
  using Strings = std::vector<std::string>;

  storage.SetString(kStorageID, Service::kStorageType, kTypeVPN);

  const auto setup_wg_and_static_addr = [&](const std::string* wg_prop_addr,
                                            const std::string* static_addr) {
    driver_ = new MockVPNDriver(&manager_, VPNType::kWireGuard);
    service_ = new VPNServiceInTest(&manager_, driver_);
    service_->set_storage_id(kStorageID);
    if (wg_prop_addr) {
      driver_->args()->Set<Strings>(kWireGuardIPAddress, {*wg_prop_addr});
    }
    if (static_addr) {
      KeyValueStore args;
      args.Set<std::string>(kAddressProperty, *static_addr);
      Error not_used_err;
      service_->mutable_store()->SetKeyValueStoreProperty(
          kStaticIPConfigProperty, args, &not_used_err);
    }
    service_->MigrateDeprecatedStorage(&storage);
  };

  setup_wg_and_static_addr(nullptr, &kAddr1);
  EXPECT_EQ(driver_->args()->Get<Strings>(kWireGuardIPAddress),
            Strings{kAddr1});
  EXPECT_EQ(service_->static_network_config_for_testing().ipv4_address,
            std::nullopt);

  setup_wg_and_static_addr(&kAddr2, &kAddr1);
  EXPECT_EQ(driver_->args()->Get<Strings>(kWireGuardIPAddress),
            Strings{kAddr2});
  EXPECT_EQ(service_->static_network_config_for_testing().ipv4_address,
            std::nullopt);

  setup_wg_and_static_addr(&kAddr2, nullptr);
  EXPECT_EQ(driver_->args()->Get<Strings>(kWireGuardIPAddress),
            Strings{kAddr2});
  EXPECT_EQ(service_->static_network_config_for_testing().ipv4_address,
            std::nullopt);
}

}  // namespace shill
