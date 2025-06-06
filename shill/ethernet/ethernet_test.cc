// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/ethernet.h"

#include <linux/ethtool.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/ether.h
#include <linux/sockios.h>
#include <netinet/ether.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_executor.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/mock_rtnl_handler.h>
#include <chromeos/net-base/mock_socket.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/ethernet/mock_eap_listener.h"
#include "shill/ethernet/mock_ethernet_eap_provider.h"
#include "shill/ethernet/mock_ethernet_provider.h"
#include "shill/ethernet/mock_ethernet_service.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_eap_credentials.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/network/mock_dhcp_controller.h"
#include "shill/network/mock_network.h"
#include "shill/network/network.h"
#include "shill/supplicant/mock_supplicant_eap_state_handler.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_process_proxy.h"
#include "shill/supplicant/supplicant_manager.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::EndsWith;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Pointer;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

namespace shill {
namespace {
const net_base::IPAddress kIPv4Addr =
    *net_base::IPAddress::CreateFromString("192.168.1.1");
const net_base::IPAddress kIPv6Addr =
    *net_base::IPAddress::CreateFromString("fe80::1aa9:5ff:abcd:1234");
}  // namespace

class TestEthernet : public Ethernet {
 public:
  TestEthernet(Manager* manager,
               const std::string& link_name,
               net_base::MacAddress mac_address,
               int interface_index)
      : Ethernet(manager, link_name, mac_address, interface_index) {}

  ~TestEthernet() override = default;

  MOCK_METHOD(std::optional<net_base::MacAddress>,
              ReadMacAddressFromFile,
              (const base::FilePath& file_path),
              (override));
};

class EthernetTest : public testing::Test {
 public:
  EthernetTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        eap_listener_(new MockEapListener()),
        mock_eap_service_(new MockService(&manager_)),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()),
        supplicant_process_proxy_(new NiceMock<MockSupplicantProcessProxy>()) {
    manager_.set_patchpanel_client_for_testing(
        std::make_unique<patchpanel::FakeClient>());
    ethernet_ = new TestEthernet(&manager_, ifname_, hwaddr_, ifindex_);
    mock_service_ = new MockEthernetService(
        &manager_, ethernet_->weak_ptr_factory_.GetWeakPtr());
  }
  ~EthernetTest() override = default;

  void SetUp() override {
    ethernet_->rtnl_handler_ = &rtnl_handler_;

    auto dhcp_controller_factory =
        std::make_unique<MockDHCPControllerFactory>();
    dhcp_controller_factory_ = dhcp_controller_factory.get();
    ethernet_->GetPrimaryNetwork()
        ->set_legacy_dhcp_controller_factory_for_testing(
            std::move(dhcp_controller_factory));
    ethernet_->GetPrimaryNetwork()->set_dhcp_controller_factory_for_testing(
        std::make_unique<MockDHCPControllerFactory>());

    EXPECT_CALL(manager_, UpdateEnabledTechnologies()).Times(AnyNumber());

    ethernet_->eap_listener_.reset(eap_listener_);  // Transfers ownership.
    EXPECT_CALL(manager_, ethernet_eap_provider())
        .WillRepeatedly(Return(&ethernet_eap_provider_));
    ethernet_eap_provider_.set_service(mock_eap_service_);
    // Transfers ownership.
    manager_.supplicant_manager()->set_proxy(supplicant_process_proxy_);

    EXPECT_CALL(manager_, ethernet_provider())
        .WillRepeatedly(Return(&ethernet_provider_));

    ON_CALL(*mock_service_, technology())
        .WillByDefault(Return(Technology::kEthernet));

    auto socket_factory = std::make_unique<net_base::MockSocketFactory>();
    socket_factory_ = socket_factory.get();
    ethernet_->socket_factory_ = std::move(socket_factory);

    // We do not care about Sockets at most of test cases.
    // In order to let RunEthtoolCmd() succeed we need to return a positive
    // number for Ioctl
    ON_CALL(*socket_factory_, Create).WillByDefault([&]() {
      auto socket = std::make_unique<net_base::MockSocket>();
      ON_CALL(*socket, Ioctl(SIOCETHTOOL, _)).WillByDefault(Return(1));
      return socket;
    });
  }

  void TearDown() override {
    ethernet_eap_provider_.set_service(nullptr);
    ethernet_->eap_listener_.reset();
    Mock::VerifyAndClearExpectations(&manager_);
  }

  MockNetwork* SetMockNetwork() {
    auto network = std::make_unique<NiceMock<MockNetwork>>(
        ifindex_, ifname_, Technology::kEthernet);
    MockNetwork* network_p = network.get();
    ethernet_->set_network_for_testing(std::move(network));
    return network_p;
  }

  MOCK_METHOD(void, ErrorCallback, (const Error& error));

 protected:
  int ifindex_ = 123;
  std::string ifname_ = "eth0";
  net_base::MacAddress hwaddr_{0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  RpcIdentifier dbus_path_ = RpcIdentifier("/interface/path");

  bool GetLinkUp() { return ethernet_->link_up_; }
  void SetLinkUp(bool link_up) { ethernet_->link_up_ = link_up; }
  const ServiceRefPtr& GetSelectedService() {
    return ethernet_->selected_service();
  }
  ServiceRefPtr GetService() { return ethernet_->service_; }
  void SetService(const EthernetServiceRefPtr& service) {
    ethernet_->service_ = service;
  }
  void SelectService(const EthernetServiceRefPtr& service) {
    ethernet_->SelectService(service);
  }

  void UpdateLinkSpeed() { ethernet_->UpdateLinkSpeed(); }

  const PropertyStore& GetStore() { return ethernet_->store(); }
  void StartEthernet() {
    EXPECT_CALL(ethernet_provider_, CreateService(_))
        .WillOnce(Return(mock_service_));
    EXPECT_CALL(ethernet_provider_, RegisterService(Eq(mock_service_)));
    EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(ifindex_, IFF_UP, IFF_UP));
    base::test::TestFuture<void> future;
    ethernet_->Start(base::BindOnce(&EthernetTest::OnEnabledStateChanged,
                                    future.GetCallback()));
    EXPECT_TRUE(future.Wait());
  }
  void StopEthernet() {
    EXPECT_CALL(ethernet_provider_, DeregisterService(Eq(mock_service_)));
    base::test::TestFuture<void> future;
    ethernet_->Stop(base::BindOnce(&EthernetTest::OnEnabledStateChanged,
                                   future.GetCallback()));
    EXPECT_TRUE(future.Wait());
  }
  void SetUsbEthernetMacAddressSource(const std::string& source,
                                      ResultCallback callback) {
    base::test::TestFuture<void> future;
    ethernet_->SetUsbEthernetMacAddressSource(
        source, std::move(callback).Then(future.GetCallback()));
    EXPECT_TRUE(future.Wait());
  }
  std::string GetUsbEthernetMacAddressSource(Error* error) {
    return ethernet_->GetUsbEthernetMacAddressSource(error);
  }

  void SetMacAddress(net_base::MacAddress mac_address) {
    ethernet_->set_mac_address(mac_address);
  }

  void SetBusType(const std::string& bus_type) {
    ethernet_->bus_type_ = bus_type;
  }

  bool GetIsEapAuthenticated() { return ethernet_->is_eap_authenticated_; }
  void SetIsEapAuthenticated(bool is_eap_authenticated) {
    ethernet_->is_eap_authenticated_ = is_eap_authenticated;
  }
  bool GetIsEapDetected() { return ethernet_->is_eap_detected_; }
  void SetIsEapDetected(bool is_eap_detected) {
    ethernet_->is_eap_detected_ = is_eap_detected;
  }
  const SupplicantInterfaceProxyInterface* GetSupplicantInterfaceProxy() {
    return ethernet_->supplicant_interface_proxy_.get();
  }
  const RpcIdentifier& GetSupplicantInterfacePath() {
    return ethernet_->supplicant_interface_path_;
  }
  const RpcIdentifier& GetSupplicantNetworkPath() {
    return ethernet_->supplicant_network_path_;
  }
  void SetSupplicantNetworkPath(const RpcIdentifier& network_path) {
    ethernet_->supplicant_network_path_ = network_path;
  }
  bool InvokeStartSupplicant() { return ethernet_->StartSupplicant(); }
  void InvokeStopSupplicant() { return ethernet_->StopSupplicant(); }
  bool InvokeStartEapAuthentication() {
    return ethernet_->StartEapAuthentication();
  }
  void StartSupplicant() {
    MockSupplicantInterfaceProxy* interface_proxy =
        ExpectCreateSupplicantInterfaceProxy();
    EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(dbus_path_), Return(true)));
    EXPECT_TRUE(InvokeStartSupplicant());
    EXPECT_EQ(interface_proxy, GetSupplicantInterfaceProxy());
    EXPECT_EQ(dbus_path_, GetSupplicantInterfacePath());
  }
  void TriggerOnEapDetected() { ethernet_->OnEapDetected(); }
  void TriggerCertification(const std::string& subject, uint32_t depth) {
    ethernet_->CertificationTask(subject, depth);
  }
  void TriggerTryEapAuthentication() { ethernet_->TryEapAuthenticationTask(); }

  MockSupplicantInterfaceProxy* ExpectCreateSupplicantInterfaceProxy() {
    MockSupplicantInterfaceProxy* proxy = supplicant_interface_proxy_.get();
    EXPECT_CALL(control_interface_,
                CreateSupplicantInterfaceProxy(_, dbus_path_))
        .WillOnce(Return(ByMove(std::move(supplicant_interface_proxy_))));
    return proxy;
  }

  EventDispatcherForTest dispatcher_;
  MockControl control_interface_;
  NiceMock<MockMetrics> metrics_;
  MockManager manager_;
  scoped_refptr<TestEthernet> ethernet_;

  MockEthernetEapProvider ethernet_eap_provider_;

  // Owned by Ethernet instance, but tracked here for expectations.
  MockEapListener* eap_listener_;
  net_base::MockSocketFactory* socket_factory_;
  MockDHCPControllerFactory* dhcp_controller_factory_;

  scoped_refptr<MockService> mock_eap_service_;
  std::unique_ptr<MockSupplicantInterfaceProxy> supplicant_interface_proxy_;
  MockSupplicantProcessProxy* supplicant_process_proxy_;

  net_base::MockRTNLHandler rtnl_handler_;
  scoped_refptr<MockEthernetService> mock_service_;
  MockEthernetProvider ethernet_provider_;

 private:
  static void OnEnabledStateChanged(base::OnceClosure quit_closure,
                                    const Error& error) {
    std::move(quit_closure).Run();
  }
};

TEST_F(EthernetTest, Construct) {
  EXPECT_FALSE(GetLinkUp());
  EXPECT_FALSE(GetIsEapAuthenticated());
  EXPECT_FALSE(GetIsEapDetected());
  EXPECT_TRUE(GetStore().Contains(kEapAuthenticationCompletedProperty));
  EXPECT_TRUE(GetStore().Contains(kEapAuthenticatorDetectedProperty));
  EXPECT_EQ(nullptr, GetService());
}

TEST_F(EthernetTest, StartStop) {
  StartEthernet();
  Service* service = GetService().get();
  EXPECT_EQ(service, mock_service_);
  StopEthernet();
}

TEST_F(EthernetTest, LinkEvent) {
  StartEthernet();

  // Link-down event while already down.
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);
  EXPECT_CALL(*eap_listener_, Start()).Times(0);
  ethernet_->LinkEvent(0, IFF_LOWER_UP);
  EXPECT_FALSE(GetLinkUp());
  EXPECT_FALSE(GetIsEapDetected());
  Mock::VerifyAndClearExpectations(&manager_);

  // Link-up event while down.
  EXPECT_CALL(manager_, UpdateService(Pointer(mock_service_)));
  EXPECT_CALL(*mock_service_, OnVisibilityChanged());
  EXPECT_CALL(*eap_listener_, Start());

  ethernet_->LinkEvent(IFF_LOWER_UP, 0);
  EXPECT_TRUE(GetLinkUp());
  EXPECT_FALSE(GetIsEapDetected());
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(mock_service_.get());

  // Link-up event while already up.
  EXPECT_CALL(manager_, UpdateService(_)).Times(0);
  EXPECT_CALL(*mock_service_, OnVisibilityChanged()).Times(0);
  EXPECT_CALL(*eap_listener_, Start()).Times(0);
  ethernet_->LinkEvent(IFF_LOWER_UP, 0);
  EXPECT_TRUE(GetLinkUp());
  EXPECT_FALSE(GetIsEapDetected());
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(mock_service_.get());

  // Link-down event while up.
  SetIsEapDetected(true);
  // This is done in SetUp, but we have to reestablish this after calling
  // VerifyAndClearExpectations() above.
  EXPECT_CALL(manager_, ethernet_eap_provider())
      .WillRepeatedly(Return(&ethernet_eap_provider_));
  EXPECT_CALL(ethernet_eap_provider_,
              ClearCredentialChangeCallback(ethernet_.get()));
  EXPECT_CALL(*eap_listener_, Stop());
  EXPECT_CALL(manager_, UpdateService(Pointer(GetService().get())));
  EXPECT_CALL(*mock_service_, OnVisibilityChanged());
  ethernet_->LinkEvent(0, IFF_LOWER_UP);
  EXPECT_FALSE(GetLinkUp());
  EXPECT_FALSE(GetIsEapDetected());

  // Restore this expectation during shutdown.
  EXPECT_CALL(manager_, UpdateEnabledTechnologies()).Times(AnyNumber());
  EXPECT_CALL(manager_, ethernet_provider())
      .WillRepeatedly(Return(&ethernet_provider_));

  StopEthernet();
}

TEST_F(EthernetTest, ConnectToLinkDown) {
  StartEthernet();
  SetLinkUp(false);
  EXPECT_EQ(nullptr, GetSelectedService());
  EXPECT_CALL(*dhcp_controller_factory_, Create).Times(0);
  EXPECT_CALL(*mock_service_, SetState(_)).Times(0);
  ethernet_->ConnectTo(mock_service_.get());
  EXPECT_EQ(nullptr, GetSelectedService());
  StopEthernet();
}

TEST_F(EthernetTest, ConnectToSuccess) {
  StartEthernet();
  SetLinkUp(true);
  EXPECT_EQ(nullptr, GetSelectedService());
  EXPECT_CALL(*dhcp_controller_factory_, Create).WillOnce([this]() {
    auto dhcp_controller = std::make_unique<MockDHCPController>(
        nullptr, nullptr, nullptr, nullptr, ifname_, Technology::kEthernet,
        DHCPController::Options{}, base::DoNothing(), base::DoNothing());
    EXPECT_CALL(*dhcp_controller, RenewIP).WillOnce(Return(true));
    return dhcp_controller;
  });
  EXPECT_CALL(*mock_service_, SetState(Service::kStateConfiguring));
  ethernet_->ConnectTo(mock_service_.get());
  dispatcher_.task_environment().RunUntilIdle();
  EXPECT_EQ(GetService(), GetSelectedService());
  Mock::VerifyAndClearExpectations(mock_service_.get());

  EXPECT_CALL(*mock_service_, SetState(Service::kStateIdle));
  ethernet_->DisconnectFrom(mock_service_.get());
  EXPECT_EQ(nullptr, GetSelectedService());
  StopEthernet();
}

TEST_F(EthernetTest, OnEapDetected) {
  EXPECT_FALSE(GetIsEapDetected());
  EXPECT_CALL(*eap_listener_, Stop());
  EXPECT_CALL(ethernet_eap_provider_,
              SetCredentialChangeCallback(ethernet_.get(), _));
  TriggerOnEapDetected();
  EXPECT_TRUE(GetIsEapDetected());
}

TEST_F(EthernetTest, TryEapAuthenticationNotConnectableNotAuthenticated) {
  SetService(mock_service_);
  EXPECT_CALL(*mock_eap_service_, Is8021xConnectable()).WillOnce(Return(false));
  NiceScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       EndsWith("EAP Service lacks 802.1X credentials; "
                                "not doing EAP authentication.")));
  TriggerTryEapAuthentication();
  SetService(nullptr);
}

TEST_F(EthernetTest, TryEapAuthenticationNotConnectableAuthenticated) {
  SetService(mock_service_);
  SetIsEapAuthenticated(true);
  EXPECT_CALL(*mock_eap_service_, Is8021xConnectable()).WillOnce(Return(false));
  NiceScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       EndsWith("EAP Service lost 802.1X credentials; "
                                "terminating EAP authentication.")));
  TriggerTryEapAuthentication();
  EXPECT_FALSE(GetIsEapAuthenticated());
}

TEST_F(EthernetTest, TryEapAuthenticationEapNotDetected) {
  SetService(mock_service_);
  EXPECT_CALL(*mock_eap_service_, Is8021xConnectable()).WillOnce(Return(true));
  NiceScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_WARNING, _,
                       EndsWith("EAP authenticator not detected; "
                                "not doing EAP authentication.")));
  TriggerTryEapAuthentication();
}

TEST_F(EthernetTest, StartSupplicant) {
  // Save the mock proxy pointers before the Ethernet instance accepts it.
  MockSupplicantInterfaceProxy* interface_proxy =
      supplicant_interface_proxy_.get();
  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;

  StartSupplicant();

  // Starting it again should not invoke another call to create an interface.
  Mock::VerifyAndClearExpectations(process_proxy);
  EXPECT_CALL(*process_proxy, CreateInterface(_, _)).Times(0);
  EXPECT_TRUE(InvokeStartSupplicant());

  // Also, the mock pointers should remain; if the MockProxyFactory was
  // invoked again, they would be nullptr.
  EXPECT_EQ(interface_proxy, GetSupplicantInterfaceProxy());
  EXPECT_EQ(dbus_path_, GetSupplicantInterfacePath());
}

TEST_F(EthernetTest, StartSupplicantWithInterfaceExistsException) {
  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;
  MockSupplicantInterfaceProxy* interface_proxy =
      ExpectCreateSupplicantInterfaceProxy();
  EXPECT_CALL(*process_proxy, CreateInterface(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*process_proxy, GetInterface(ifname_, _))
      .WillOnce(DoAll(SetArgPointee<1>(dbus_path_), Return(true)));
  EXPECT_TRUE(InvokeStartSupplicant());
  EXPECT_EQ(interface_proxy, GetSupplicantInterfaceProxy());
  EXPECT_EQ(dbus_path_, GetSupplicantInterfacePath());
}

TEST_F(EthernetTest, StartSupplicantWithUnknownException) {
  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;
  EXPECT_CALL(*process_proxy, CreateInterface(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*process_proxy, GetInterface(ifname_, _)).WillOnce(Return(false));
  EXPECT_FALSE(InvokeStartSupplicant());
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxy());
  EXPECT_EQ(RpcIdentifier(""), GetSupplicantInterfacePath());
}

TEST_F(EthernetTest, StartEapAuthentication) {
  MockSupplicantInterfaceProxy* interface_proxy =
      supplicant_interface_proxy_.get();

  StartSupplicant();
  SetService(mock_service_);

  EXPECT_CALL(*mock_service_, ClearEAPCertification());
  MockEapCredentials mock_eap_credentials;
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));
  EXPECT_CALL(mock_eap_credentials, PopulateSupplicantProperties(_, _, _));
  EXPECT_CALL(*interface_proxy, RemoveNetwork(_)).Times(0);
  EXPECT_CALL(*interface_proxy, AddNetwork(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*interface_proxy, SelectNetwork(_)).Times(0);
  EXPECT_CALL(*interface_proxy, EAPLogon()).Times(0);
  EXPECT_FALSE(InvokeStartEapAuthentication());
  Mock::VerifyAndClearExpectations(mock_service_.get());
  Mock::VerifyAndClearExpectations(mock_eap_service_.get());
  Mock::VerifyAndClearExpectations(interface_proxy);
  EXPECT_EQ(RpcIdentifier(""), GetSupplicantNetworkPath());

  EXPECT_CALL(*mock_service_, ClearEAPCertification());
  EXPECT_CALL(*interface_proxy, RemoveNetwork(_)).Times(0);
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));
  EXPECT_CALL(mock_eap_credentials, PopulateSupplicantProperties(_, _, _));
  const RpcIdentifier kFirstNetworkPath("/network/first-path");
  EXPECT_CALL(*interface_proxy, AddNetwork(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFirstNetworkPath), Return(true)));
  EXPECT_CALL(*interface_proxy, SelectNetwork(Eq(kFirstNetworkPath)));
  EXPECT_CALL(*interface_proxy, EAPLogon());
  EXPECT_TRUE(InvokeStartEapAuthentication());
  Mock::VerifyAndClearExpectations(mock_service_.get());
  Mock::VerifyAndClearExpectations(mock_eap_service_.get());
  Mock::VerifyAndClearExpectations(&mock_eap_credentials);
  Mock::VerifyAndClearExpectations(interface_proxy);
  EXPECT_EQ(kFirstNetworkPath, GetSupplicantNetworkPath());

  EXPECT_CALL(*mock_service_, ClearEAPCertification());
  EXPECT_CALL(*interface_proxy, RemoveNetwork(Eq(kFirstNetworkPath)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));
  EXPECT_CALL(mock_eap_credentials, PopulateSupplicantProperties(_, _, _));
  const RpcIdentifier kSecondNetworkPath("/network/second-path");
  EXPECT_CALL(*interface_proxy, AddNetwork(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSecondNetworkPath), Return(true)));
  EXPECT_CALL(*interface_proxy, SelectNetwork(Eq(kSecondNetworkPath)));
  EXPECT_CALL(*interface_proxy, EAPLogon());
  EXPECT_TRUE(InvokeStartEapAuthentication());
  EXPECT_EQ(kSecondNetworkPath, GetSupplicantNetworkPath());
}

TEST_F(EthernetTest, StartEapAuthenticationWithCaCertExperiment) {
  MockSupplicantInterfaceProxy* interface_proxy =
      supplicant_interface_proxy_.get();
  MockEapCredentials mock_eap_credentials;

  StartSupplicant();
  SetService(mock_service_);

  // Return value for the GetEapService()->GetCACertExperimentPhase() is not
  // defined, a default value EapCredentials::CaCertExperimentPhase::kDisabled
  // should be used in PopulateSupplicantProperties().
  EXPECT_CALL(*mock_service_, ClearEAPCertification());
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));
  EXPECT_CALL(mock_eap_credentials,
              PopulateSupplicantProperties(
                  _, _, EapCredentials::CaCertExperimentPhase::kDisabled));
  EXPECT_CALL(*interface_proxy, RemoveNetwork(_)).Times(0);
  EXPECT_CALL(*interface_proxy, AddNetwork(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*interface_proxy, SelectNetwork(_)).Times(0);
  EXPECT_CALL(*interface_proxy, EAPLogon()).Times(0);

  EXPECT_FALSE(InvokeStartEapAuthentication());
  Mock::VerifyAndClearExpectations(mock_service_.get());
  Mock::VerifyAndClearExpectations(mock_eap_service_.get());
  Mock::VerifyAndClearExpectations(interface_proxy);
  EXPECT_EQ(RpcIdentifier(""), GetSupplicantNetworkPath());

  // Return value for the GetEapService()->GetCACertExperimentPhase() is
  // defined, PopulateSupplicantProperties() should be called with it.
  EXPECT_CALL(*mock_service_, ClearEAPCertification());
  EXPECT_CALL(*interface_proxy, RemoveNetwork(_)).Times(0);
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));
  EXPECT_CALL(*mock_eap_service_, GetCACertExperimentPhase())
      .WillOnce(Return(EapCredentials::CaCertExperimentPhase::kPhase2));
  EXPECT_CALL(mock_eap_credentials,
              PopulateSupplicantProperties(
                  _, _, EapCredentials::CaCertExperimentPhase::kPhase2));
  const RpcIdentifier kFirstNetworkPath("/network/first-path");
  EXPECT_CALL(*interface_proxy, AddNetwork(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFirstNetworkPath), Return(true)));
  EXPECT_CALL(*interface_proxy, SelectNetwork(Eq(kFirstNetworkPath)));
  EXPECT_CALL(*interface_proxy, EAPLogon());

  EXPECT_TRUE(InvokeStartEapAuthentication());
  Mock::VerifyAndClearExpectations(mock_service_.get());
  Mock::VerifyAndClearExpectations(mock_eap_service_.get());
  Mock::VerifyAndClearExpectations(&mock_eap_credentials);
  Mock::VerifyAndClearExpectations(interface_proxy);
  EXPECT_EQ(kFirstNetworkPath, GetSupplicantNetworkPath());
}

TEST_F(EthernetTest, StopSupplicant) {
  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;
  MockSupplicantInterfaceProxy* interface_proxy =
      supplicant_interface_proxy_.get();
  StartSupplicant();
  SetIsEapAuthenticated(true);
  SetSupplicantNetworkPath(RpcIdentifier("/network/1"));
  EXPECT_CALL(*interface_proxy, EAPLogoff()).WillOnce(Return(true));
  EXPECT_CALL(*process_proxy, RemoveInterface(Eq(dbus_path_)))
      .WillOnce(Return(true));
  InvokeStopSupplicant();
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxy());
  EXPECT_EQ(RpcIdentifier(""), GetSupplicantInterfacePath());
  EXPECT_EQ(RpcIdentifier(""), GetSupplicantNetworkPath());
  EXPECT_FALSE(GetIsEapAuthenticated());
}

TEST_F(EthernetTest, Certification) {
  StartEthernet();
  const std::string kSubjectName("subject-name");
  const uint32_t kDepth = 123;
  // Should not crash due to no service_.
  TriggerCertification(kSubjectName, kDepth);
  EXPECT_CALL(*mock_service_, AddEAPCertification(kSubjectName, kDepth));
  SetService(mock_service_);
  TriggerCertification(kSubjectName, kDepth);
  StopEthernet();
}

TEST_F(EthernetTest, ReportEapEventAuthStillNotDoneNoFailure) {
  NiceScopedMockLog log;
  StartEthernet();
  SetSupplicantNetworkPath(RpcIdentifier("/network/1"));
  SetService(mock_service_);
  MockEapCredentials mock_eap_credentials;
  EXPECT_CALL(*mock_eap_service_, GetCACertExperimentPhase())
      .WillOnce(Return(EapCredentials::CaCertExperimentPhase::kDisabled));
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));

  // It is difficult to mock or replace eap_state_handler_, so real
  // eap_state_handler_.ParseStatus() will be called.
  // This combination of status and parameter should set
  // is_eap_authenticated_ == false, failure == kFailureNone, and should set
  // metric to Metrics::kEapEventCertVerificationSuccess.
  const std::string kEAPStatus("remote certificate verification");
  const std::string kEAPParameter("success");
  EXPECT_CALL(
      mock_eap_credentials,
      ReportEapEventMetric(_, EapCredentials::CaCertExperimentPhase::kDisabled,
                           Metrics::kEapEventCertVerificationSuccess))
      .Times(1);
  EXPECT_CALL(log, Log(logging::LOGGING_WARNING, _,
                       testing::HasSubstr("EAP authentication failure")))
      .Times(0);
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       testing::HasSubstr("EAP authentication success")))
      .Times(0);
  ethernet_->EAPEventTask(kEAPStatus, kEAPParameter);

  EXPECT_FALSE(ethernet_->is_eap_authenticated_);
  StopEthernet();
}

TEST_F(EthernetTest, ReportEapEventAuthFailure) {
  StartEthernet();
  SetSupplicantNetworkPath(RpcIdentifier("/network/1"));
  SetService(mock_service_);
  MockEapCredentials mock_eap_credentials;
  EXPECT_CALL(*mock_eap_service_, GetCACertExperimentPhase())
      .WillOnce(Return(EapCredentials::CaCertExperimentPhase::kDisabled));
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));

  // It is difficult to mock or replace eap_state_handler_, so real
  // eap_state_handler_.ParseStatus() will be called.
  // This combination of status and parameter should set
  // is_eap_authenticated_ == false, failure == kFailurePinMissing, and should
  // set metric to Metrics::kEapEventPinMissing.
  ethernet_->SetIsEapAuthenticated(true);
  const std::string kEAPStatus("eap parameter needed");
  const std::string kEAPParameter("PIN");
  EXPECT_CALL(
      mock_eap_credentials,
      ReportEapEventMetric(_, EapCredentials::CaCertExperimentPhase::kDisabled,
                           Metrics::kEapEventPinMissing))
      .Times(1);

  ethernet_->EAPEventTask(kEAPStatus, kEAPParameter);

  EXPECT_FALSE(ethernet_->is_eap_authenticated_);
  StopEthernet();
}

TEST_F(EthernetTest, ReportEapEventAuthSuccessful) {
  StartEthernet();
  SetService(mock_service_);
  SetSupplicantNetworkPath(RpcIdentifier("/network/1"));
  MockEapCredentials mock_eap_credentials;

  EXPECT_CALL(*mock_eap_service_, GetCACertExperimentPhase())
      .WillOnce(Return(EapCredentials::CaCertExperimentPhase::kDisabled));
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));

  // It is difficult to mock or replace eap_state_handler_, so real
  // eap_state_handler_.ParseStatus() will be called.
  // This combination of status and parameter should set
  // is_eap_authenticated_ == true and should set metric
  // to Metrics::kEapEventAuthCompletedSuccess.
  ethernet_->SetIsEapAuthenticated(false);
  const std::string kEAPStatus("completion");
  const std::string kEAPParameter("success");
  EXPECT_CALL(
      mock_eap_credentials,
      ReportEapEventMetric(_, EapCredentials::CaCertExperimentPhase::kDisabled,
                           Metrics::kEapEventAuthCompletedSuccess))
      .Times(1);

  ethernet_->EAPEventTask(kEAPStatus, kEAPParameter);

  EXPECT_TRUE(ethernet_->is_eap_authenticated_);
  StopEthernet();
}

TEST_F(EthernetTest, ReportEapEventWithActiveCaExperiment) {
  StartEthernet();
  SetService(mock_service_);
  SetSupplicantNetworkPath(RpcIdentifier("/network/1"));
  MockEapCredentials mock_eap_credentials;

  EXPECT_CALL(*mock_eap_service_, GetCACertExperimentPhase())
      .WillOnce(Return(EapCredentials::CaCertExperimentPhase::kPhase1));
  EXPECT_CALL(*mock_eap_service_, eap())
      .WillOnce(Return(&mock_eap_credentials));

  const std::string kEAPStatus("random");
  const std::string kEAPParameter("random");
  EXPECT_CALL(mock_eap_credentials,
              ReportEapEventMetric(
                  _, EapCredentials::CaCertExperimentPhase::kPhase1, _))
      .Times(1);

  ethernet_->EAPEventTask(kEAPStatus, kEAPParameter);

  StopEthernet();
}

MATCHER_P(ErrorEquals, expected_error_type, "") {
  return arg.type() == expected_error_type;
}

TEST_F(EthernetTest, SetUsbEthernetMacAddressSourceInvalidArguments) {
  SetBusType(kDeviceBusTypeUsb);

  EXPECT_CALL(*this, ErrorCallback(ErrorEquals(Error::kInvalidArguments)));
  SetUsbEthernetMacAddressSource(
      "invalid_value",
      base::BindOnce(&EthernetTest::ErrorCallback, base::Unretained(this)));
}

TEST_F(EthernetTest, SetUsbEthernetMacAddressSourceNotSupportedForNonUsb) {
  SetBusType(kDeviceBusTypePci);

  EXPECT_CALL(*this, ErrorCallback(ErrorEquals(Error::kIllegalOperation)));
  SetUsbEthernetMacAddressSource(
      kUsbEthernetMacAddressSourceUsbAdapterMac,
      base::BindOnce(&EthernetTest::ErrorCallback, base::Unretained(this)));
}

TEST_F(EthernetTest,
       SetUsbEthernetMacAddressSourceNotSupportedEmptyFileWithMac) {
  SetBusType(kDeviceBusTypeUsb);
  EXPECT_CALL(*this, ErrorCallback(ErrorEquals(Error::kNotFound)));
  SetUsbEthernetMacAddressSource(
      kUsbEthernetMacAddressSourceDesignatedDockMac,
      base::BindOnce(&EthernetTest::ErrorCallback, base::Unretained(this)));
}

TEST_F(EthernetTest, SetUsbEthernetMacAddressSourceNetlinkError) {
  SetBusType(kDeviceBusTypeUsb);

  constexpr net_base::MacAddress kBuiltinAdapterMacAddress(0xab, 0xcd, 0xef,
                                                           0x12, 0x34, 0x56);

  EXPECT_CALL(*ethernet_.get(), ReadMacAddressFromFile)
      .WillOnce(Return(kBuiltinAdapterMacAddress));

  EXPECT_CALL(rtnl_handler_, SetInterfaceMac(ethernet_->interface_index(),
                                             kBuiltinAdapterMacAddress, _))
      .WillOnce(WithArg<2>(
          Invoke([](base::OnceCallback<void(int32_t)> response_callback) {
            ASSERT_TRUE(!response_callback.is_null());
            std::move(response_callback).Run(1 /* error */);
          })));

  EXPECT_CALL(*this, ErrorCallback(ErrorEquals(Error::kOperationFailed)));
  SetUsbEthernetMacAddressSource(
      kUsbEthernetMacAddressSourceBuiltinAdapterMac,
      base::BindOnce(&EthernetTest::ErrorCallback, base::Unretained(this)));

  EXPECT_EQ(hwaddr_, ethernet_->mac_address());
}

TEST_F(EthernetTest, SetUsbEthernetMacAddressSource) {
  SetBusType(kDeviceBusTypeUsb);

  constexpr net_base::MacAddress kBuiltinAdapterMacAddress(0xab, 0xcd, 0xef,
                                                           0x12, 0x34, 0x56);

  EXPECT_CALL(*ethernet_.get(), ReadMacAddressFromFile)
      .WillOnce(Return(kBuiltinAdapterMacAddress));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMac(ethernet_->interface_index(),
                                             kBuiltinAdapterMacAddress, _))
      .WillOnce(WithArg<2>(
          Invoke([](base::OnceCallback<void(int32_t)> response_callback) {
            ASSERT_FALSE(response_callback.is_null());
            std::move(response_callback).Run(0 /* error */);
          })));

  EXPECT_CALL(*this, ErrorCallback(ErrorEquals(Error::kSuccess)));
  SetUsbEthernetMacAddressSource(
      kUsbEthernetMacAddressSourceBuiltinAdapterMac,
      base::BindOnce(&EthernetTest::ErrorCallback, base::Unretained(this)));

  EXPECT_EQ(kBuiltinAdapterMacAddress, ethernet_->mac_address());
  EXPECT_EQ(GetUsbEthernetMacAddressSource(nullptr),
            kUsbEthernetMacAddressSourceBuiltinAdapterMac);
}

TEST_F(EthernetTest, SetMacAddressNoServiceStorageIdentifierChange) {
  constexpr net_base::MacAddress kMacAddress(0x12, 0x34, 0x56, 0xab, 0xcd,
                                             0xef);

  scoped_refptr<StrictMock<MockProfile>> mock_profile(
      new StrictMock<MockProfile>(&manager_));
  mock_service_->set_profile(mock_profile);
  mock_service_->SetStorageIdentifier("some_ethernet_identifier");
  EXPECT_CALL(*mock_profile.get(), AbandonService(_)).Times(0);
  EXPECT_CALL(*mock_profile.get(), AdoptService(_)).Times(0);

  SetMacAddress(kMacAddress);
  EXPECT_EQ(kMacAddress, ethernet_->mac_address());

  // Must set nullptr to avoid mock objects leakage.
  mock_service_->set_profile(nullptr);
}

TEST_F(EthernetTest, SetMacAddressServiceStorageIdentifierChange) {
  StartEthernet();
  constexpr net_base::MacAddress kMacAddress(0x12, 0x34, 0x56, 0xab, 0xcd,
                                             0xef);

  scoped_refptr<StrictMock<MockProfile>> mock_profile(
      new StrictMock<MockProfile>(&manager_));
  mock_service_->set_profile(mock_profile);
  EXPECT_CALL(*mock_profile.get(), AbandonService(Pointer(mock_service_)));
  EXPECT_CALL(*mock_profile.get(), AdoptService(Pointer(mock_service_)));

  SetMacAddress(kMacAddress);
  EXPECT_EQ(kMacAddress, ethernet_->mac_address());

  // Must set nullptr to avoid mock objects leakage.
  mock_service_->set_profile(nullptr);
  StopEthernet();
}

TEST_F(EthernetTest, UpdateLinkSpeed) {
  EXPECT_CALL(*mock_service_, SetUplinkSpeedKbps(_));

  SelectService(mock_service_);
  UpdateLinkSpeed();
}

TEST_F(EthernetTest, UpdateLinkSpeedNoSelectedService) {
  EXPECT_CALL(*mock_service_, SetUplinkSpeedKbps(_)).Times(0);

  SelectService(nullptr);
  UpdateLinkSpeed();
}

TEST_F(EthernetTest, RunEthtoolCmdSuccess) {
  struct ethtool_cmd ecmd;
  struct ifreq ifr;

  memset(&ecmd, 0, sizeof(ecmd));
  ecmd.cmd = ETHTOOL_GSET;
  ifr.ifr_data = &ecmd;

  EXPECT_CALL(*socket_factory_,
              Create(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_IP))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Ioctl(SIOCETHTOOL, _)).WillOnce(Return(1));
        return socket;
      });
  EXPECT_TRUE(ethernet_->RunEthtoolCmd(&ifr));
}

TEST_F(EthernetTest, RunEthtoolCmdFail) {
  struct ethtool_cmd ecmd;
  struct ifreq ifr;

  memset(&ecmd, 0, sizeof(ecmd));
  ecmd.cmd = ETHTOOL_GSET;
  ifr.ifr_data = &ecmd;

  EXPECT_CALL(*socket_factory_,
              Create(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_IP))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Ioctl(SIOCETHTOOL, _))
            .WillOnce(Return(std::nullopt));
        return socket;
      });
  EXPECT_FALSE(ethernet_->RunEthtoolCmd(&ifr));
}

TEST_F(EthernetTest, ReachabilityEvent_Online) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  // Network is connect with verified Internet access.
  auto mock_network = SetMockNetwork();
  ON_CALL(*mock_network, IsConnected()).WillByDefault(Return(true));
  ON_CALL(*mock_network, HasInternetConnectivity()).WillByDefault(Return(true));

  // REACHABLE neighbor events are ignored.
  EXPECT_CALL(*mock_network, RequestNetworkValidation).Times(0);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kGateway,
                                         Status::kReachable);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kGatewayAndDnsServer,
                                         Status::kReachable);
  Mock::VerifyAndClearExpectations(mock_network);

  // FAILED gateway neighbor events triggers network validation.
  EXPECT_CALL(
      *mock_network,
      RequestNetworkValidation(
          NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable));
  ethernet_->OnNeighborReachabilityEvent(
      ethernet_->interface_index(), kIPv4Addr, Role::kGateway, Status::kFailed);
  Mock::VerifyAndClearExpectations(mock_network);

  EXPECT_CALL(
      *mock_network,
      RequestNetworkValidation(
          NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable));
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kGatewayAndDnsServer,
                                         Status::kFailed);
  Mock::VerifyAndClearExpectations(mock_network);
}

TEST_F(EthernetTest, ReachabilityEvent_NotOnline) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  // Network is connected but with no Internet access.
  auto mock_network = SetMockNetwork();
  ON_CALL(*mock_network, IsConnected()).WillByDefault(Return(true));
  ON_CALL(*mock_network, HasInternetConnectivity())
      .WillByDefault(Return(false));

  // FAILED neighbor events are ignored.
  EXPECT_CALL(*mock_network, RequestNetworkValidation).Times(0);
  ethernet_->OnNeighborReachabilityEvent(
      ethernet_->interface_index(), kIPv4Addr, Role::kGateway, Status::kFailed);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kGatewayAndDnsServer,
                                         Status::kFailed);
  Mock::VerifyAndClearExpectations(mock_network);

  // REACHABLE neighbor events triggers network validation.
  EXPECT_CALL(*mock_network,
              RequestNetworkValidation(
                  NetworkMonitor::ValidationReason::kEthernetGatewayReachable));
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kGateway,
                                         Status::kReachable);
  Mock::VerifyAndClearExpectations(mock_network);

  EXPECT_CALL(*mock_network,
              RequestNetworkValidation(
                  NetworkMonitor::ValidationReason::kEthernetGatewayReachable));
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kGatewayAndDnsServer,
                                         Status::kReachable);
  Mock::VerifyAndClearExpectations(mock_network);
}

TEST_F(EthernetTest, ReachabilityEvent_DNSServers) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  auto mock_network = SetMockNetwork();
  ON_CALL(*mock_network, IsConnected()).WillByDefault(Return(true));
  ON_CALL(*mock_network, HasInternetConnectivity()).WillByDefault(Return(true));

  // DNS neighbor events are always ignored.
  EXPECT_CALL(*mock_network, RequestNetworkValidation).Times(0);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kDnsServer,
                                         Status::kFailed);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kDnsServer,
                                         Status::kFailed);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kDnsServer,
                                         Status::kReachable);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kDnsServer,
                                         Status::kReachable);
  Mock::VerifyAndClearExpectations(mock_network);

  EXPECT_CALL(*mock_network, RequestNetworkValidation).Times(0);
  ON_CALL(*mock_network, HasInternetConnectivity())
      .WillByDefault(Return(false));
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kDnsServer,
                                         Status::kFailed);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kDnsServer,
                                         Status::kFailed);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv4Addr, Role::kDnsServer,
                                         Status::kReachable);
  ethernet_->OnNeighborReachabilityEvent(ethernet_->interface_index(),
                                         kIPv6Addr, Role::kDnsServer,
                                         Status::kReachable);
  Mock::VerifyAndClearExpectations(mock_network);
}

}  // namespace shill
