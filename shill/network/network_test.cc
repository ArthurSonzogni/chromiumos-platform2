// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback_helpers.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "shill/http_request.h"
#include "shill/ipconfig.h"
#include "shill/metrics.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/mock_dhcp_controller.h"
#include "shill/network/mock_dhcp_provider.h"
#include "shill/network/mock_network.h"
#include "shill/network/mock_network_applier.h"
#include "shill/network/mock_proc_fs_stub.h"
#include "shill/network/mock_slaac_controller.h"
#include "shill/network/network_applier.h"
#include "shill/portal_detector.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"

namespace shill {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InvokeWithoutArgs;
using ::testing::IsEmpty;
using ::testing::Mock;
using ::testing::Ne;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::WithArg;

constexpr int kTestIfindex = 123;
constexpr char kTestIfname[] = "eth_test";
constexpr auto kTestTechnology = Technology::kWiFi;

// IPv4 properties from DHCP.
constexpr char kIPv4DHCPAddress[] = "192.168.1.2";
constexpr int kIPv4DHCPPrefix = 24;
constexpr char kIPv4DHCPGateway[] = "192.168.1.1";
constexpr char kIPv4DHCPNameServer[] = "192.168.1.3";
constexpr int kIPv4DHCPMTU = 1400;

// IPv4 properties from link protocol (e.g., VPN or Cellular).
constexpr char kIPv4LinkProtocolAddress[] = "192.168.3.2";
constexpr int kIPv4LinkProtocolPrefix = 24;
constexpr char kIPv4LinkProtocolGateway[] = "192.168.3.1";
constexpr char kIPv4LinkProtocolNameServer[] = "192.168.3.3";
constexpr int kIPv4LinkProtocolMTU = 1410;

// IPv4 properties from static IP config. Note that MTU is not set here, so that
// we can verify if the config is pure static IP config or merged with others.
constexpr char kIPv4StaticAddress[] = "10.0.8.2";
constexpr int kIPv4StaticPrefix = 16;
constexpr char kIPv4StaticGateway[] = "10.0.8.1";
constexpr char kIPv4StaticNameServer[] = "10.0.8.3";

// IPv6 properties from SLAAC.
constexpr char kIPv6SLAACAddress[] = "fd00::2";
constexpr int kIPv6SLAACPrefix = 64;
constexpr char kIPv6SLAACGateway[] = "fd00::1";
constexpr char kIPv6SLAACNameserver[] = "fd00::3";

// IPv6 properties from link protocol (e.g., VPN).
constexpr char kIPv6LinkProtocolAddress[] = "fd00:1::2";
constexpr int kIPv6LinkProtocolPrefix = 96;
constexpr char kIPv6LinkProtocolGateway[] = "fd00:1::1";
constexpr char kIPv6LinkProtocolNameserver[] = "fd00:1::3";

MATCHER_P(ContainsAddressAndRoute, family, "") {
  if (family == net_base::IPFamily::kIPv4) {
    return arg & NetworkApplier::Area::kIPv4Address &&
           arg & NetworkApplier::Area::kIPv4Route;
  } else if (family == net_base::IPFamily::kIPv6) {
    return arg & NetworkApplier::Area::kIPv6Route;
  }
  return false;
}

net_base::NetworkConfig CreateIPv4NetworkConfig(
    const std::string& addr,
    int prefix_len,
    const std::string& gateway,
    const std::vector<std::string>& dns_servers,
    std::optional<int> mtu) {
  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromStringAndPrefix(addr, prefix_len);
  config.ipv4_gateway = *net_base::IPv4Address::CreateFromString(gateway);
  config.dns_servers = {};
  std::transform(dns_servers.begin(), dns_servers.end(),
                 std::back_inserter(config.dns_servers),
                 [](const std::string& dns) {
                   return *net_base::IPAddress::CreateFromString(dns);
                 });
  config.mtu = mtu;
  return config;
}

// TODO(b/232177767): This function is IPv4-only currently. Implement the IPv6
// part when necessary.
IPConfig::Properties NetworkConfigToIPProperties(
    const net_base::NetworkConfig& config) {
  IPConfig::Properties props = {};
  props.address_family = net_base::IPFamily::kIPv4;
  props.UpdateFromNetworkConfig(config);
  return props;
}

class MockPortalDetector : public PortalDetector {
 public:
  MockPortalDetector() : PortalDetector(nullptr, {}, base::DoNothing()) {}
  MockPortalDetector(const MockPortalDetector&) = delete;
  MockPortalDetector& operator=(const MockPortalDetector&) = delete;
  ~MockPortalDetector() = default;

  MOCK_METHOD(void,
              Start,
              (const std::string& ifname,
               net_base::IPFamily,
               const std::vector<net_base::IPAddress>&,
               const std::string& logging_tag),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, IsInProgress, (), (const, override));
  MOCK_METHOD(void, ResetAttemptDelays, (), (override));
};

class MockConnectionDiagnostics : public ConnectionDiagnostics {
 public:
  MockConnectionDiagnostics()
      : ConnectionDiagnostics(
            kTestIfname,
            kTestIfindex,
            *net_base::IPAddress::CreateFromString(kIPv4DHCPAddress),
            *net_base::IPAddress::CreateFromString(kIPv4DHCPGateway),
            {*net_base::IPAddress::CreateFromString(kIPv4DHCPNameServer)},
            nullptr,
            nullptr,
            base::DoNothing()) {}
  MockConnectionDiagnostics(const MockConnectionDiagnostics&) = delete;
  MockConnectionDiagnostics& operator=(const MockConnectionDiagnostics&) =
      delete;
  ~MockConnectionDiagnostics() = default;

  MOCK_METHOD(bool, Start, (const net_base::HttpUrl& url), (override));
};

// Allows us to fake/mock some functions in this test.
class NetworkInTest : public Network {
 public:
  NetworkInTest(int interface_index,
                const std::string& interface_name,
                Technology technology,
                bool fixed_ip_params,
                ControlInterface* control_interface,
                EventDispatcher* dispatcher,
                Metrics* metrics,
                NetworkApplier* network_applier)
      : Network(interface_index,
                interface_name,
                technology,
                fixed_ip_params,
                control_interface,
                dispatcher,
                metrics,
                network_applier) {
    ON_CALL(*this, ApplyNetworkConfig)
        .WillByDefault(
            [](NetworkApplier::Area area, base::OnceClosure callback) {
              std::move(callback).Run();
            });
  }

  MOCK_METHOD(std::unique_ptr<SLAACController>,
              CreateSLAACController,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<PortalDetector>,
              CreatePortalDetector,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ConnectionDiagnostics>,
              CreateConnectionDiagnostics,
              (const net_base::IPAddress& ip_address,
               const net_base::IPAddress& gateway,
               const std::vector<net_base::IPAddress>& dns_list),
              (override));
  MOCK_METHOD(void,
              ApplyNetworkConfig,
              (NetworkApplier::Area area, base::OnceClosure callback),
              (override));
};

class NetworkTest : public ::testing::Test {
 public:
  NetworkTest() : manager_(&control_interface_, &dispatcher_, nullptr) {
    network_ = std::make_unique<NiceMock<NetworkInTest>>(
        kTestIfindex, kTestIfname, kTestTechnology,
        /*fixed_ip_params=*/false, &control_interface_, &dispatcher_, &metrics_,
        &network_applier_);
    network_->set_dhcp_provider_for_testing(&dhcp_provider_);
    network_->RegisterEventHandler(&event_handler_);
    network_->RegisterEventHandler(&event_handler2_);
    proc_fs_ = dynamic_cast<MockProcFsStub*>(network_->set_proc_fs_for_testing(
        std::make_unique<NiceMock<MockProcFsStub>>(kTestIfname)));
    EXPECT_CALL(dhcp_provider_, CreateController).Times(0);
    ON_CALL(*network_, CreateSLAACController()).WillByDefault([this]() {
      auto ret = std::make_unique<NiceMock<MockSLAACController>>();
      slaac_controller_ = ret.get();
      return ret;
    });
  }
  ~NetworkTest() override { network_ = nullptr; }

  // Expects calling CreateController() on DHCPProvider, and the following
  // RequestIP() call will return |request_ip_result|. The pointer to the
  // returned DHCPController will be stored in |dhcp_controller_|.
  void ExpectCreateDHCPController(bool request_ip_result) {
    EXPECT_CALL(dhcp_provider_, CreateController)
        .WillOnce(InvokeWithoutArgs([request_ip_result, this]() {
          auto controller = std::make_unique<NiceMock<MockDHCPController>>(
              &control_interface_, kTestIfname);
          EXPECT_CALL(*controller, RequestIP())
              .WillOnce(Return(request_ip_result));
          dhcp_controller_ = controller.get();
          return controller;
        }));
  }

  void SetNetworkStateToConnected() {
    network_->set_state_for_testing(Network::State::kConnected);
    network_->set_primary_family_for_testing(net_base::IPFamily::kIPv4);
  }

  // Sets a fake DHCPv4 config to allow network validation to start.
  void SetNetworkStateForPortalDetection() {
    SetNetworkStateToConnected();
    net_base::NetworkConfig config;
    config.ipv4_address =
        *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
    config.ipv4_gateway =
        *net_base::IPv4Address::CreateFromString("192.168.1.1");
    config.dns_servers = {
        *net_base::IPAddress::CreateFromString("8.8.8.8"),
        *net_base::IPAddress::CreateFromString("8.8.4.4"),
    };
    network_->set_dhcp_network_config_for_testing(config);
  }

  // Ensure local() and gateway() being available for portal detection.
  void SetNetworkStateForConnectionDiagnostic() {
    SetNetworkStateToConnected();
    const std::string ipv4_addr_str = "192.168.1.1";
    auto config = std::make_unique<net_base::NetworkConfig>();
    config->ipv4_address =
        net_base::IPv4CIDR::CreateFromStringAndPrefix(ipv4_addr_str, 32);
    config->ipv4_gateway =
        net_base::IPv4Address::CreateFromString(ipv4_addr_str);
    config->dns_servers = {
        *net_base::IPAddress::CreateFromString(ipv4_addr_str)};
    network_->set_link_protocol_network_config(std::move(config));
  }

 protected:
  // Order does matter in this group. See the constructor.
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  StrictMock<MockMetrics> metrics_;

  MockDHCPProvider dhcp_provider_;
  MockNetworkEventHandler event_handler_;
  MockNetworkEventHandler event_handler2_;
  NiceMock<MockNetworkApplier> network_applier_;

  std::unique_ptr<NiceMock<NetworkInTest>> network_;

  // Variables owned by |network_|. Not guaranteed valid even if it's not null.
  MockDHCPController* dhcp_controller_ = nullptr;
  MockSLAACController* slaac_controller_ = nullptr;
  MockProcFsStub* proc_fs_ = nullptr;
};

TEST_F(NetworkTest, EventHandlerRegistration) {
  MockNetworkEventHandler event_handler3;
  std::vector<MockNetworkEventHandler*> all_event_handlers = {
      &event_handler_, &event_handler2_, &event_handler3};

  // EventHandler #3 is not yet registered.
  EXPECT_CALL(event_handler_, OnNetworkStopped(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkStopped(network_->interface_index(), _));
  EXPECT_CALL(event_handler3, OnNetworkStopped).Times(0);
  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }

  // All EventHandlers are registered.
  network_->RegisterEventHandler(&event_handler3);
  for (auto* ev : all_event_handlers) {
    EXPECT_CALL(*ev, OnNetworkStopped(network_->interface_index(), _));
  }
  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }

  // EventHandlers can only be registered once.
  network_->RegisterEventHandler(&event_handler_);
  network_->RegisterEventHandler(&event_handler2_);
  network_->RegisterEventHandler(&event_handler3);
  for (auto* ev : all_event_handlers) {
    EXPECT_CALL(*ev, OnNetworkStopped(network_->interface_index(), _)).Times(1);
  }
  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }

  // EventHandlers can be unregistered.
  network_->UnregisterEventHandler(&event_handler_);
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_,
              OnNetworkStopped(network_->interface_index(), _));
  EXPECT_CALL(event_handler3, OnNetworkStopped(network_->interface_index(), _));
  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }

  // All EventHandlers are unregistered.
  for (auto* ev : all_event_handlers) {
    network_->UnregisterEventHandler(ev);
  }
  for (auto* ev : all_event_handlers) {
    EXPECT_CALL(*ev, OnNetworkStopped).Times(0);
  }
  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }

  // Network destruction
  network_->RegisterEventHandler(&event_handler_);
  network_->RegisterEventHandler(&event_handler2_);
  EXPECT_CALL(event_handler_, OnNetworkDestroyed(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnNetworkDestroyed(network_->interface_index()));
  EXPECT_CALL(event_handler3, OnNetworkDestroyed).Times(0);
  network_ = nullptr;
  for (auto* ev : all_event_handlers) {
    Mock::VerifyAndClearExpectations(ev);
  }
}

// Verifies that a handler can unregister itself in the callback.
TEST_F(NetworkTest, UnregisterHandlerInCallback) {
  EXPECT_CALL(event_handler_, OnNetworkStopped)
      .WillOnce(InvokeWithoutArgs(
          [this] { network_->UnregisterEventHandler(&this->event_handler_); }));
  EXPECT_CALL(event_handler2_, OnNetworkStopped);

  network_->Start(Network::StartOptions{.accept_ra = true});
  network_->Stop();
}

TEST_F(NetworkTest, OnNetworkStoppedCalledOnStopAfterStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_,
              OnNetworkStopped(network_->interface_index(), false))
      .Times(1);
  EXPECT_CALL(event_handler2_,
              OnNetworkStopped(network_->interface_index(), false))
      .Times(1);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Additional Stop() should not trigger the callback.
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStopWithoutStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  network_->Stop();
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

TEST_F(NetworkTest, OnNetworkStoppedCalledOnDHCPFailure) {
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_,
              OnNetworkStopped(network_->interface_index(), true))
      .Times(1);
  EXPECT_CALL(event_handler2_,
              OnNetworkStopped(network_->interface_index(), true))
      .Times(1);
  ASSERT_NE(dhcp_controller_, nullptr);
  dhcp_controller_->TriggerDropCallback(/*is_voluntary=*/false);
}

TEST_F(NetworkTest, EnableARPFilteringOnStart) {
  ExpectCreateDHCPController(true);
  EXPECT_CALL(*proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv4, "arp_announce", "2"))
      .WillOnce(Return(true));
  EXPECT_CALL(*proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv4, "arp_ignore", "1"))
      .WillOnce(Return(true));
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

TEST_F(NetworkTest, EnableIPv6FlagsLinkProtocol) {
  // Not interested in IPv4 flags in this test.
  EXPECT_CALL(*proc_fs_, SetIPFlag(net_base::IPFamily::kIPv4, _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv6, "disable_ipv6", "0"))
      .WillOnce(Return(true));
  auto link_protocol_properties = std::make_unique<IPConfig::Properties>();
  link_protocol_properties->address = "2001:db8:abcd::1234";
  network_->set_link_protocol_network_config(
      std::make_unique<net_base::NetworkConfig>(
          IPConfig::Properties::ToNetworkConfig(
              nullptr, link_protocol_properties.get())));
  network_->Start(Network::StartOptions{});
}

// Verifies that the DHCP options in Network::Start() is properly used when
// creating the DHCPController.
TEST_F(NetworkTest, DHCPOptions) {
  constexpr char kHostname[] = "hostname";
  constexpr char kLeaseName[] = "lease-name";

  ON_CALL(dhcp_provider_, CreateController)
      .WillByDefault(InvokeWithoutArgs([this]() {
        return std::make_unique<NiceMock<MockDHCPController>>(
            &control_interface_, kTestIfname);
      }));

  DHCPProvider::Options opts = {
      .use_arp_gateway = true,
      .lease_name = kLeaseName,
      .hostname = kHostname,
  };
  EXPECT_CALL(dhcp_provider_,
              CreateController(
                  _,
                  AllOf(Field(&DHCPProvider::Options::use_arp_gateway, true),
                        Field(&DHCPProvider::Options::lease_name, kLeaseName),
                        Field(&DHCPProvider::Options::hostname, kHostname)),
                  _));
  network_->Start({.dhcp = opts});

  // When there is static IP, |use_arp_gateway| will be forced to false.
  network_->Stop();
  EXPECT_CALL(dhcp_provider_,
              CreateController(
                  _, Field(&DHCPProvider::Options::use_arp_gateway, false), _));
  net_base::NetworkConfig static_config;
  static_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  network_->OnStaticIPConfigChanged(static_config);
  network_->Start({.dhcp = opts});
}

TEST_F(NetworkTest, DHCPRenew) {
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
  EXPECT_CALL(*dhcp_controller_, RenewIP()).WillOnce(Return(true));
  EXPECT_TRUE(network_->RenewDHCPLease());
}

TEST_F(NetworkTest, DHCPRenewWithoutController) {
  EXPECT_FALSE(network_->RenewDHCPLease());
}

TEST_F(NetworkTest, NeighborReachabilityEvents) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  const std::string ipv4_addr_str = "192.168.1.1";
  const std::string ipv6_addr_str = "fe80::1aa9:5ff:abcd:1234";
  const auto ipv4_addr = *net_base::IPAddress::CreateFromString(ipv4_addr_str);
  const auto ipv6_addr = *net_base::IPAddress::CreateFromString(ipv6_addr_str);
  SetNetworkStateToConnected();

  auto network_config = std::make_unique<net_base::NetworkConfig>();
  network_config->ipv4_gateway =
      *net_base::IPv4Address::CreateFromString(ipv4_addr_str);
  network_config->ipv6_gateway =
      *net_base::IPv6Address::CreateFromString(ipv6_addr_str);
  // Placeholder addresses to let Network believe this is a valid configuration.
  network_config->ipv4_address =
      *net_base::IPv4CIDR::CreateFromStringAndPrefix(ipv4_addr_str, 32);
  network_config->ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromStringAndPrefix(ipv6_addr_str, 120)};
  network_->set_link_protocol_network_config(std::move(network_config));

  // Connected network with IPv4 configured, reachability event matching the
  // IPv4 gateway.
  EXPECT_CALL(event_handler_, OnNeighborReachabilityEvent(
                                  network_->interface_index(), ipv4_addr,
                                  Role::kGateway, Status::kReachable))
      .Times(1);
  EXPECT_CALL(event_handler2_, OnNeighborReachabilityEvent(
                                   network_->interface_index(), ipv4_addr,
                                   Role::kGateway, Status::kReachable))
      .Times(1);
  patchpanel::Client::NeighborReachabilityEvent event1;
  event1.ifindex = 1;
  event1.ip_addr = ipv4_addr_str;
  event1.role = Role::kGateway;
  event1.status = Status::kReachable;
  network_->OnNeighborReachabilityEvent(event1);
  EXPECT_TRUE(network_->ipv4_gateway_found());
  EXPECT_FALSE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Connected network with IPv6 configured, reachability event matching the
  // IPv6 gateway.
  EXPECT_CALL(event_handler_,
              OnNeighborReachabilityEvent(network_->interface_index(),
                                          ipv6_addr, Role::kGatewayAndDnsServer,
                                          Status::kReachable))
      .Times(1);
  EXPECT_CALL(event_handler2_,
              OnNeighborReachabilityEvent(network_->interface_index(),
                                          ipv6_addr, Role::kGatewayAndDnsServer,
                                          Status::kReachable))
      .Times(1);
  patchpanel::Client::NeighborReachabilityEvent event2;
  event2.ifindex = 1;
  event2.ip_addr = ipv6_addr_str;
  event2.role = Role::kGatewayAndDnsServer;
  event2.status = Status::kReachable;
  network_->OnNeighborReachabilityEvent(event2);
  EXPECT_TRUE(network_->ipv4_gateway_found());
  EXPECT_TRUE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Signals for unrelated gateway addresses are ignored
  patchpanel::Client::NeighborReachabilityEvent event3;
  event3.ifindex = 1;
  event3.ip_addr = "172.16.1.1";
  event3.role = Role::kGateway;
  event3.status = Status::kReachable;
  patchpanel::Client::NeighborReachabilityEvent event4;
  event4.ifindex = 1;
  event4.ip_addr = "fe80::1122:ccdd:7890:f1g2";
  event4.role = Role::kGateway;
  event4.status = Status::kReachable;
  network_->OnNeighborReachabilityEvent(event3);
  network_->OnNeighborReachabilityEvent(event4);
  EXPECT_CALL(event_handler_, OnNeighborReachabilityEvent).Times(0);
  EXPECT_CALL(event_handler2_, OnNeighborReachabilityEvent).Times(0);
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Check that gateway reachability state is reset when the network starts
  // again.
  ExpectCreateDHCPController(true);
  network_->Stop();
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{},
                                        .accept_ra = true});
  network_->set_state_for_testing(Network::State::kConfiguring);
  EXPECT_FALSE(network_->ipv4_gateway_found());
  EXPECT_FALSE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(&dhcp_controller_);

  // Not connected yet, reachability signals are ignored.
  EXPECT_CALL(event_handler_, OnNeighborReachabilityEvent).Times(0);
  EXPECT_CALL(event_handler2_, OnNeighborReachabilityEvent).Times(0);
  network_->OnNeighborReachabilityEvent(event1);
  network_->OnNeighborReachabilityEvent(event2);
  EXPECT_FALSE(network_->ipv4_gateway_found());
  EXPECT_FALSE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Connected and IPv4 configured, IPv6 reachability signals are ignored.
  EXPECT_CALL(event_handler_, OnNeighborReachabilityEvent(
                                  network_->interface_index(), ipv4_addr,
                                  Role::kGateway, Status::kReachable))
      .Times(1);
  EXPECT_CALL(event_handler2_, OnNeighborReachabilityEvent(
                                   network_->interface_index(), ipv4_addr,
                                   Role::kGateway, Status::kReachable))
      .Times(1);
  network_config = std::make_unique<net_base::NetworkConfig>();
  network_config->ipv4_address =
      *net_base::IPv4CIDR::CreateFromStringAndPrefix(ipv4_addr_str, 32);
  network_config->ipv4_gateway =
      *net_base::IPv4Address::CreateFromString(ipv4_addr_str);
  network_->set_link_protocol_network_config(std::move(network_config));

  SetNetworkStateToConnected();
  network_->OnNeighborReachabilityEvent(event1);
  network_->OnNeighborReachabilityEvent(event2);
  EXPECT_TRUE(network_->ipv4_gateway_found());
  EXPECT_FALSE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Disconnected, reconnected and IPv6 configured, IPv4 reachability signals
  // are ignored.
  ExpectCreateDHCPController(true);
  EXPECT_CALL(event_handler_,
              OnNeighborReachabilityEvent(network_->interface_index(),
                                          ipv6_addr, Role::kGatewayAndDnsServer,
                                          Status::kReachable))
      .Times(1);
  EXPECT_CALL(event_handler2_,
              OnNeighborReachabilityEvent(network_->interface_index(),
                                          ipv6_addr, Role::kGatewayAndDnsServer,
                                          Status::kReachable))
      .Times(1);
  network_->Stop();
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{},
                                        .accept_ra = true});

  network_config = std::make_unique<net_base::NetworkConfig>();
  network_config->ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromStringAndPrefix(ipv6_addr_str, 120)};
  network_config->ipv6_gateway =
      *net_base::IPv6Address::CreateFromString(ipv6_addr_str);
  network_->set_link_protocol_network_config(std::move(network_config));

  SetNetworkStateToConnected();
  network_->OnNeighborReachabilityEvent(event1);
  network_->OnNeighborReachabilityEvent(event2);
  EXPECT_FALSE(network_->ipv4_gateway_found());
  EXPECT_TRUE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(&dhcp_controller_);

  // Link monitoring disabled by configuration
  ExpectCreateDHCPController(true);
  EXPECT_CALL(event_handler_, OnNeighborReachabilityEvent).Times(0);
  EXPECT_CALL(event_handler2_, OnNeighborReachabilityEvent).Times(0);
  network_->Stop();
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{},
                                        .accept_ra = true,
                                        .ignore_link_monitoring = true});
  network_->set_ipconfig(
      std::make_unique<IPConfig>(&control_interface_, kTestIfname));
  network_->set_ip6config(
      std::make_unique<IPConfig>(&control_interface_, kTestIfname));

  network_config = std::make_unique<net_base::NetworkConfig>();
  network_config->ipv4_address =
      *net_base::IPv4CIDR::CreateFromStringAndPrefix(ipv4_addr_str, 32);
  network_config->ipv4_gateway =
      *net_base::IPv4Address::CreateFromString(ipv4_addr_str);
  network_config->ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromStringAndPrefix(ipv6_addr_str, 120)};
  network_config->ipv6_gateway =
      *net_base::IPv6Address::CreateFromString(ipv6_addr_str);
  network_->set_link_protocol_network_config(std::move(network_config));

  SetNetworkStateToConnected();
  network_->OnNeighborReachabilityEvent(event1);
  network_->OnNeighborReachabilityEvent(event2);
  EXPECT_FALSE(network_->ipv4_gateway_found());
  EXPECT_FALSE(network_->ipv6_gateway_found());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(&dhcp_controller_);

  network_->set_ipconfig(nullptr);
  network_->set_ip6config(nullptr);
}

TEST_F(NetworkTest, NeighborReachabilityEventsMetrics) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  patchpanel::Client::NeighborReachabilityEvent ipv4_event;
  ipv4_event.ip_addr = "192.168.11.34";
  ipv4_event.status = Status::kFailed;

  patchpanel::Client::NeighborReachabilityEvent ipv6_event;
  ipv6_event.ip_addr = "2001:db8::abcd:1234";
  ipv6_event.status = Status::kFailed;

  auto wifi_network = std::make_unique<NiceMock<NetworkInTest>>(
      kTestIfindex, kTestIfname, Technology::kWiFi,
      /*fixed_ip_params=*/false, &control_interface_, &dispatcher_, &metrics_,
      &network_applier_);
  wifi_network->set_ignore_link_monitoring_for_testing(true);

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                    Technology::kWiFi, Metrics::kNeighborIPv4GatewayFailure));
  ipv4_event.role = Role::kGateway;
  wifi_network->OnNeighborReachabilityEvent(ipv4_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                    Technology::kWiFi, Metrics::kNeighborIPv4DNSServerFailure));
  ipv4_event.role = Role::kDnsServer;
  wifi_network->OnNeighborReachabilityEvent(ipv4_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kWiFi,
                            Metrics::kNeighborIPv4GatewayAndDNSServerFailure));
  ipv4_event.role = Role::kGatewayAndDnsServer;
  wifi_network->OnNeighborReachabilityEvent(ipv4_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                    Technology::kWiFi, Metrics::kNeighborIPv6GatewayFailure));
  ipv6_event.role = Role::kGateway;
  wifi_network->OnNeighborReachabilityEvent(ipv6_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                    Technology::kWiFi, Metrics::kNeighborIPv6DNSServerFailure));
  ipv6_event.role = Role::kDnsServer;
  wifi_network->OnNeighborReachabilityEvent(ipv6_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kWiFi,
                            Metrics::kNeighborIPv6GatewayAndDNSServerFailure));
  ipv6_event.role = Role::kGatewayAndDnsServer;
  wifi_network->OnNeighborReachabilityEvent(ipv6_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  auto eth_network = std::make_unique<NiceMock<NetworkInTest>>(
      kTestIfindex, kTestIfname, Technology::kEthernet,
      /*fixed_ip_params=*/false, &control_interface_, &dispatcher_, &metrics_,
      &network_applier_);
  eth_network->set_ignore_link_monitoring_for_testing(true);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kEthernet,
                            Metrics::kNeighborIPv6DNSServerFailure));
  ipv6_event.role = Role::kDnsServer;
  eth_network->OnNeighborReachabilityEvent(ipv6_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kEthernet,
                            Metrics::kNeighborIPv6GatewayAndDNSServerFailure));
  ipv6_event.role = Role::kGatewayAndDnsServer;
  eth_network->OnNeighborReachabilityEvent(ipv6_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kEthernet,
                            Metrics::kNeighborIPv4DNSServerFailure));
  ipv4_event.role = Role::kDnsServer;
  eth_network->OnNeighborReachabilityEvent(ipv4_event);
  Mock::VerifyAndClearExpectations(&metrics_);

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure,
                            Technology::kEthernet,
                            Metrics::kNeighborIPv4GatewayAndDNSServerFailure));
  ipv4_event.role = Role::kGatewayAndDnsServer;
  eth_network->OnNeighborReachabilityEvent(ipv4_event);
  Mock::VerifyAndClearExpectations(&metrics_);
}

TEST_F(NetworkTest, PortalDetectionStopBeforeStart) {
  EXPECT_CALL(event_handler_, OnNetworkValidationStop).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStop).Times(0);
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  network_->StopPortalDetection();
}

TEST_F(NetworkTest, PortalDetectionRestartBeforeStart) {
  EXPECT_CALL(event_handler_, OnNetworkValidationStart).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStart).Times(0);
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_FALSE(network_->RestartPortalDetection());
}

TEST_F(NetworkTest, PortalDetectionNotConnected) {
  EXPECT_FALSE(network_->IsConnected());
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_CALL(*network_, CreatePortalDetector()).Times(0);
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()))
      .Times(0);
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()))
      .Times(0);
  EXPECT_FALSE(network_->StartPortalDetection(
      Network::ValidationReason::kServicePropertyUpdate));
  EXPECT_FALSE(
      network_->StartPortalDetection(Network::ValidationReason::kDBusRequest));
}

TEST_F(NetworkTest, PortalDetectionNoDNS) {
  SetNetworkStateToConnected();
  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  config.dns_servers = {};
  network_->set_dhcp_network_config_for_testing(config);

  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_CALL(*network_, CreatePortalDetector()).Times(0);
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()))
      .Times(0);
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()))
      .Times(0);
  EXPECT_FALSE(network_->StartPortalDetection(
      Network::ValidationReason::kServicePropertyUpdate));
  EXPECT_FALSE(
      network_->StartPortalDetection(Network::ValidationReason::kDBusRequest));
}

TEST_F(NetworkTest, PortalDetectionRequestsInitializesPortalDetector) {
  int ifindex = network_->interface_index();
  SetNetworkStateForPortalDetection();

  std::vector<Network::ValidationReason> all_reasons = {
      Network::ValidationReason::kNetworkConnectionUpdate,
      Network::ValidationReason::kServiceReorder,
      Network::ValidationReason::kServicePropertyUpdate,
      Network::ValidationReason::kManagerPropertyUpdate,
      Network::ValidationReason::kDBusRequest,
      Network::ValidationReason::kEthernetGatewayUnreachable,
      Network::ValidationReason::kEthernetGatewayReachable,
  };

  for (auto request : all_reasons) {
    MockPortalDetector* portal_detector = new MockPortalDetector();
    EXPECT_CALL(*network_, CreatePortalDetector())
        .WillOnce([portal_detector]() {
          return std::unique_ptr<MockPortalDetector>(portal_detector);
        });
    EXPECT_CALL(*portal_detector, Start);
    EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(event_handler_, OnNetworkValidationStart(ifindex));
    EXPECT_CALL(event_handler2_, OnNetworkValidationStart(ifindex));
    EXPECT_FALSE(network_->IsPortalDetectionInProgress());
    EXPECT_TRUE(network_->StartPortalDetection(request));
    Mock::VerifyAndClearExpectations(portal_detector);

    EXPECT_CALL(event_handler_, OnNetworkValidationStop(ifindex));
    EXPECT_CALL(event_handler2_, OnNetworkValidationStop(ifindex));
    EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
    network_->StopPortalDetection();
    Mock::VerifyAndClearExpectations(&event_handler_);
    Mock::VerifyAndClearExpectations(&event_handler2_);
  }
}

TEST_F(NetworkTest, PortalDetectionRequestDoesNotResetPortalDetector) {
  SetNetworkStateForPortalDetection();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreatePortalDetector()).Times(0);
  EXPECT_CALL(event_handler_, OnNetworkValidationStart).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStart).Times(0);

  std::vector<Network::ValidationReason> reasons = {
      Network::ValidationReason::kEthernetGatewayUnreachable,
      Network::ValidationReason::kManagerPropertyUpdate,
      Network::ValidationReason::kServicePropertyUpdate};
  EXPECT_CALL(*portal_detector, Start).Times(0);
  EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(0);
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  for (auto request : reasons) {
    EXPECT_TRUE(network_->IsPortalDetectionInProgress());
    EXPECT_TRUE(network_->StartPortalDetection(request));
  }
  Mock::VerifyAndClearExpectations(portal_detector);
}

TEST_F(NetworkTest, PortalDetectionRequestResetPortalDetectorRestartDelay) {
  SetNetworkStateForPortalDetection();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreatePortalDetector()).Times(0);
  EXPECT_CALL(event_handler_, OnNetworkValidationStart).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStart).Times(0);

  std::vector<Network::ValidationReason> reasons = {
      Network::ValidationReason::kDBusRequest,
      Network::ValidationReason::kEthernetGatewayReachable,
      Network::ValidationReason::kServiceReorder,
  };
  EXPECT_CALL(*portal_detector, Start).Times(0);
  EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(reasons.size());
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  for (auto request : reasons) {
    EXPECT_TRUE(network_->StartPortalDetection(request));
  }
}

TEST_F(NetworkTest, PortalDetectionRequestResetsPortalDetector) {
  SetNetworkStateForPortalDetection();

  std::vector<Network::ValidationReason> validation_reasons = {
      Network::ValidationReason::kNetworkConnectionUpdate,
  };
  for (auto request : validation_reasons) {
    MockPortalDetector* portal_detector = new MockPortalDetector();
    EXPECT_CALL(*network_, CreatePortalDetector())
        .WillRepeatedly([portal_detector]() {
          return std::unique_ptr<MockPortalDetector>(portal_detector);
        });
    EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
    EXPECT_CALL(*portal_detector, Start);
    EXPECT_CALL(event_handler_,
                OnNetworkValidationStart(network_->interface_index()));
    EXPECT_CALL(event_handler2_,
                OnNetworkValidationStart(network_->interface_index()));
    EXPECT_TRUE(network_->StartPortalDetection(request));
    Mock::VerifyAndClearExpectations(portal_detector);
    Mock::VerifyAndClearExpectations(&event_handler_);
    Mock::VerifyAndClearExpectations(&event_handler2_);
  }
}

TEST_F(NetworkTest, PortalDetectionStartSuccess) {
  SetNetworkStateForPortalDetection();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  EXPECT_CALL(*network_, CreatePortalDetector()).WillOnce([portal_detector]() {
    return std::unique_ptr<MockPortalDetector>(portal_detector);
  });
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_CALL(*portal_detector, Start);
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_TRUE(network_->StartPortalDetection(
      Network::ValidationReason::kServicePropertyUpdate));
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
  Mock::VerifyAndClearExpectations(portal_detector);
}

TEST_F(NetworkTest, PortalDetectionStartStop) {
  SetNetworkStateForPortalDetection();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  EXPECT_CALL(*network_, CreatePortalDetector()).WillOnce([portal_detector]() {
    return std::unique_ptr<MockPortalDetector>(portal_detector);
  });
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_CALL(*portal_detector, Start);
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_TRUE(network_->StartPortalDetection(
      Network::ValidationReason::kServicePropertyUpdate));
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(portal_detector);

  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStop(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStop(network_->interface_index()));
  network_->StopPortalDetection();
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
}

TEST_F(NetworkTest, PortalDetectionRestartSuccess) {
  SetNetworkStateForPortalDetection();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  EXPECT_CALL(*network_, CreatePortalDetector()).WillOnce([portal_detector]() {
    return std::unique_ptr<MockPortalDetector>(portal_detector);
  });
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
  EXPECT_CALL(*portal_detector, Start);
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_TRUE(network_->StartPortalDetection(
      Network::ValidationReason::kServicePropertyUpdate));
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(portal_detector);

  EXPECT_CALL(*portal_detector, Start);
  EXPECT_CALL(*portal_detector, IsInProgress()).WillRepeatedly(Return(true));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStart(network_->interface_index()));
  EXPECT_TRUE(network_->RestartPortalDetection());
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
  Mock::VerifyAndClearExpectations(portal_detector);
}

TEST_F(NetworkTest, PortalDetectionResult_AfterDisconnection) {
  EXPECT_FALSE(network_->IsConnected());
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.http_status_code = 204;
  result.http_content_length = 0;
  result.https_error = HttpRequest::Error::kHTTPTimeout;
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());
  EXPECT_CALL(event_handler_, OnNetworkValidationResult).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationResult).Times(0);
  network_->OnPortalDetectorResult(result);
}

TEST_F(NetworkTest, PortalDetectionResult_PartialConnectivity) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  SetNetworkStateForConnectionDiagnostic();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.http_status_code = 204;
  result.http_content_length = 0;
  result.https_error = HttpRequest::Error::kConnectionFailure;
  result.http_duration = base::Milliseconds(100);
  result.https_duration = base::Milliseconds(200);
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());
  MockConnectionDiagnostics* conn_diag = new MockConnectionDiagnostics();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  ON_CALL(*portal_detector, IsInProgress()).WillByDefault(Return(true));
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreateConnectionDiagnostics)
      .WillOnce([conn_diag](const net_base::IPAddress&,
                            const net_base::IPAddress&,
                            const std::vector<net_base::IPAddress>&) {
        return std::unique_ptr<MockConnectionDiagnostics>(conn_diag);
      });
  EXPECT_CALL(event_handler_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              Technology::kWiFi, 204));
  EXPECT_CALL(event_handler_, OnNetworkValidationStop).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStop).Times(0);
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            network_->network_validation_result()->GetValidationState());
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
}

TEST_F(NetworkTest, PortalDetectionResult_NoConnectivity) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  SetNetworkStateForConnectionDiagnostic();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kConnection,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_error = HttpRequest::Error::kConnectionFailure;
  result.http_duration = base::Milliseconds(0);
  result.https_duration = base::Milliseconds(200);
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());
  MockConnectionDiagnostics* conn_diag = new MockConnectionDiagnostics();
  MockPortalDetector* portal_detector = new MockPortalDetector();
  ON_CALL(*portal_detector, IsInProgress()).WillByDefault(Return(true));
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreateConnectionDiagnostics)
      .WillOnce([conn_diag](const net_base::IPAddress&,
                            const net_base::IPAddress&,
                            const std::vector<net_base::IPAddress>&) {
        return std::unique_ptr<MockConnectionDiagnostics>(conn_diag);
      });
  EXPECT_CALL(event_handler_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(event_handler_, OnNetworkValidationStop).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStop).Times(0);
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            network_->network_validation_result()->GetValidationState());
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
}

TEST_F(NetworkTest, PortalDetectionResult_InternetConnectivity) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.http_duration = base::Milliseconds(100);
  result.https_duration = base::Milliseconds(200);
  result.http_status_code = 204;
  result.http_content_length = 0;
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());
  MockPortalDetector* portal_detector = new MockPortalDetector();
  ON_CALL(*portal_detector, IsInProgress()).WillByDefault(Return(true));
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreateConnectionDiagnostics).Times(0);
  EXPECT_CALL(event_handler_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                        Technology::kWiFi, 200));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              Technology::kWiFi, 204));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStop(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStop(network_->interface_index()));
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            network_->network_validation_result()->GetValidationState());
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());
}

TEST_F(NetworkTest, PortalDetectionResult_PortalRedirect) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.http_status_code = 302;
  result.http_content_length = 0;
  result.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  result.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  result.http_duration = base::Milliseconds(100);
  result.https_duration = base::Milliseconds(200);
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());
  MockPortalDetector* portal_detector = new MockPortalDetector();
  ON_CALL(*portal_detector, IsInProgress()).WillByDefault(Return(true));
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(*network_, CreateConnectionDiagnostics).Times(0);
  EXPECT_CALL(event_handler_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorPortalDiscoveryDuration,
                        Technology::kWiFi, 200));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              Technology::kWiFi, 302));
  EXPECT_CALL(event_handler_, OnNetworkValidationStop).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkValidationStop).Times(0);
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            network_->network_validation_result()->GetValidationState());
  EXPECT_TRUE(network_->IsPortalDetectionInProgress());
}

TEST_F(NetworkTest, PortalDetectionResult_PortalInvalidRedirect) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  SetNetworkStateForConnectionDiagnostic();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.http_status_code = 302;
  result.http_content_length = 0;
  result.https_error = HttpRequest::Error::kConnectionFailure;
  result.redirect_url = std::nullopt;
  result.http_duration = base::Milliseconds(100);
  result.https_duration = base::Milliseconds(200);
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  MockConnectionDiagnostics* conn_diag = new MockConnectionDiagnostics();
  EXPECT_CALL(*network_, CreateConnectionDiagnostics)
      .WillOnce([conn_diag](const net_base::IPAddress&,
                            const net_base::IPAddress&,
                            const std::vector<net_base::IPAddress>&) {
        return std::unique_ptr<MockConnectionDiagnostics>(conn_diag);
      });
  EXPECT_CALL(event_handler_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationResult(network_->interface_index(), _));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(
                  Metrics::kPortalDetectorHTTPResponseCode, Technology::kWiFi,
                  Metrics::kPortalDetectorHTTPResponseCodeIncompleteRedirect));
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            network_->network_validation_result()->GetValidationState());
}

TEST_F(NetworkTest, PortalDetectionResult_ClearAfterStop) {
  EXPECT_FALSE(network_->network_validation_result().has_value());
  SetNetworkStateForPortalDetection();
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.http_status_code = 204;
  result.http_content_length = 0;
  result.http_duration = base::Milliseconds(100);
  result.https_duration = base::Milliseconds(200);
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());
  MockPortalDetector* portal_detector = new MockPortalDetector();
  ON_CALL(*portal_detector, IsInProgress()).WillByDefault(Return(true));
  network_->set_portal_detector_for_testing(portal_detector);

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                        Technology::kWiFi, 200));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              Technology::kWiFi, 204));
  EXPECT_CALL(event_handler_,
              OnNetworkValidationStop(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnNetworkValidationStop(network_->interface_index()));
  network_->OnPortalDetectorResult(result);
  EXPECT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            network_->network_validation_result()->GetValidationState());
  EXPECT_FALSE(network_->IsPortalDetectionInProgress());

  network_->Stop();
  EXPECT_FALSE(network_->network_validation_result().has_value());
}

TEST_F(NetworkTest, IsConnectedViaTether) {
  EXPECT_FALSE(network_->IsConnectedViaTether());

  EXPECT_FALSE(network_->IsConnectedViaTether());

  DHCPv4Config::Data dhcp_data;
  const char vendor_option1[] = "ANDROID_METERED";
  dhcp_data.vendor_encapsulated_options =
      ByteArray(vendor_option1, vendor_option1 + strlen(vendor_option1));
  network_->set_dhcp_data_for_testing(dhcp_data);
  EXPECT_TRUE(network_->IsConnectedViaTether());

  const char vendor_option2[] = "Some other non-empty value";
  dhcp_data.vendor_encapsulated_options =
      ByteArray(vendor_option2, vendor_option2 + strlen(vendor_option2));
  network_->set_dhcp_data_for_testing(dhcp_data);
  EXPECT_FALSE(network_->IsConnectedViaTether());
}

// This group of tests verify the interaction between Network and Connection,
// and the events sent out from Network, on calling Network::Start() and other
// IP acquisition events.
namespace {

class NetworkStartTest : public NetworkTest {
 public:
  struct TestOptions {
    bool dhcp = false;
    bool static_ipv4 = false;
    bool link_protocol_ipv4 = false;
    bool link_protocol_ipv6 = false;
    bool accept_ra = false;
  };

  // Each value indicates a specific kind of IPConfig used in the tests.
  enum class IPConfigType {
    kNone,
    kIPv4DHCP,
    kIPv4Static,
    kIPv4LinkProtocol,
    kIPv4DHCPWithStatic,
    kIPv4LinkProtocolWithStatic,
    kIPv6SLAAC,
    kIPv6LinkProtocol,
  };

  NetworkStartTest() {
    ipv4_dhcp_config_ = CreateIPv4NetworkConfig(
        kIPv4DHCPAddress, kIPv4DHCPPrefix, kIPv4DHCPGateway,
        {kIPv4DHCPNameServer}, kIPv4DHCPMTU);
    ipv4_static_config_ = CreateIPv4NetworkConfig(
        kIPv4StaticAddress, kIPv4StaticPrefix, kIPv4StaticGateway,
        {kIPv4StaticNameServer}, std::nullopt);
    ipv4_link_protocol_config_ = CreateIPv4NetworkConfig(
        kIPv4LinkProtocolAddress, kIPv4LinkProtocolPrefix,
        kIPv4LinkProtocolGateway, {kIPv4LinkProtocolNameServer},
        kIPv4LinkProtocolMTU);

    ipv4_dhcp_props_ = NetworkConfigToIPProperties(ipv4_dhcp_config_);
    ipv4_static_props_ = NetworkConfigToIPProperties(ipv4_static_config_);
    ipv4_link_protocol_props_ =
        NetworkConfigToIPProperties(ipv4_link_protocol_config_);

    ipv4_dhcp_with_static_props_ = ipv4_static_props_;
    ipv4_dhcp_with_static_props_.mtu = kIPv4DHCPMTU;
    ipv4_link_protocol_with_static_props_ = ipv4_static_props_;
    ipv4_link_protocol_with_static_props_.mtu = kIPv4LinkProtocolMTU;

    ipv6_slaac_props_.address_family = net_base::IPFamily::kIPv6;
    ipv6_slaac_props_.method = kTypeSLAAC;
    ipv6_slaac_props_.address = kIPv6SLAACAddress;
    ipv6_slaac_props_.subnet_prefix = kIPv6SLAACPrefix;
    ipv6_slaac_props_.gateway = kIPv6SLAACGateway;
    ipv6_slaac_props_.dns_servers = {kIPv6SLAACNameserver};

    ipv6_link_protocol_props_.address_family = net_base::IPFamily::kIPv6;
    ipv6_link_protocol_props_.method = kTypeSLAAC;
    ipv6_link_protocol_props_.address = kIPv6LinkProtocolAddress;
    ipv6_link_protocol_props_.subnet_prefix = kIPv6LinkProtocolPrefix;
    ipv6_link_protocol_props_.gateway = kIPv6LinkProtocolGateway;
    ipv6_link_protocol_props_.dns_servers = {kIPv6LinkProtocolNameserver};
  }

  void InvokeStart(const TestOptions& test_opts) {
    if (test_opts.static_ipv4) {
      ConfigureStaticIPv4Config();
    }
    if (test_opts.link_protocol_ipv4 || test_opts.link_protocol_ipv6) {
      IPConfig::Properties* ipv6 =
          test_opts.link_protocol_ipv6 ? &ipv6_link_protocol_props_ : nullptr;
      IPConfig::Properties* ipv4 =
          test_opts.link_protocol_ipv4 ? &ipv4_link_protocol_props_ : nullptr;
      auto network_config = IPConfig::Properties::ToNetworkConfig(ipv4, ipv6);
      network_->set_link_protocol_network_config(
          std::make_unique<net_base::NetworkConfig>(std::move(network_config)));
    }
    Network::StartOptions start_opts{
        .dhcp = test_opts.dhcp ? std::make_optional(DHCPProvider::Options{})
                               : std::nullopt,
        .accept_ra = test_opts.accept_ra,
    };
    network_->Start(start_opts);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void ConfigureStaticIPv4Config() {
    network_->OnStaticIPConfigChanged(ipv4_static_config_);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerDHCPFailureCallback() {
    ASSERT_NE(dhcp_controller_, nullptr);
    dhcp_controller_->TriggerDropCallback(/*is_voluntary=*/false);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerDHCPOption108Callback() {
    ASSERT_NE(dhcp_controller_, nullptr);
    dhcp_controller_->TriggerDropCallback(/*is_voluntary=*/true);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerDHCPUpdateCallback() {
    ASSERT_NE(dhcp_controller_, nullptr);
    dhcp_controller_->TriggerUpdateCallback(ipv4_dhcp_config_,
                                            DHCPv4Config::Data{});
  }

  void TriggerSLAACUpdate() {
    TriggerSLAACNameServersUpdate(
        {*net_base::IPAddress::CreateFromString(kIPv6SLAACNameserver)});
    TriggerSLAACAddressUpdate();
  }

  void TriggerSLAACAddressUpdate() {
    slaac_config_.ipv6_gateway =
        *net_base::IPv6Address::CreateFromString(kIPv6SLAACGateway);
    slaac_config_.ipv6_addresses = {
        *net_base::IPv6CIDR::CreateFromStringAndPrefix(kIPv6SLAACAddress,
                                                       kIPv6SLAACPrefix)};
    EXPECT_CALL(*slaac_controller_, GetNetworkConfig())
        .WillRepeatedly(Return(slaac_config_));
    slaac_controller_->TriggerCallback(SLAACController::UpdateType::kAddress);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerSLAACAddressUpdate(net_base::IPv6CIDR address) {
    slaac_config_.ipv6_addresses = {address};
    EXPECT_CALL(*slaac_controller_, GetNetworkConfig())
        .WillRepeatedly(Return(slaac_config_));
    slaac_controller_->TriggerCallback(SLAACController::UpdateType::kAddress);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerSLAACNameServersUpdate(
      const std::vector<net_base::IPAddress>& dns_list) {
    slaac_config_.dns_servers = dns_list;
    EXPECT_CALL(*slaac_controller_, GetNetworkConfig())
        .WillRepeatedly(Return(slaac_config_));
    slaac_controller_->TriggerCallback(SLAACController::UpdateType::kRDNSS);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void ExpectConnectionUpdateFromIPConfig(IPConfigType ipconfig_type) {
    const auto expected_props = GetIPPropertiesFromType(ipconfig_type);
    const auto family = expected_props.address_family;
    EXPECT_CALL(*network_,
                ApplyNetworkConfig(ContainsAddressAndRoute(family), _));
  }

  // Verifies the IPConfigs object exposed by Network is expected.
  void VerifyIPConfigs(IPConfigType ipv4_type, IPConfigType ipv6_type) {
    if (ipv4_type == IPConfigType::kNone) {
      EXPECT_EQ(network_->ipconfig(), nullptr);
    } else {
      ASSERT_NE(network_->ipconfig(), nullptr);
    }

    if (ipv6_type == IPConfigType::kNone) {
      EXPECT_EQ(network_->ip6config(), nullptr);
    } else {
      ASSERT_NE(network_->ip6config(), nullptr);
    }
    EXPECT_EQ(IPConfig::Properties::ToNetworkConfig(
                  GetIPPropertiesPtrFromType(ipv4_type),
                  GetIPPropertiesPtrFromType(ipv6_type)),
              network_->GetNetworkConfig());
  }

  // Verifies that GetAddresses() returns all configured addresses, in the order
  // of IPv4->IPv6.
  void VerifyGetAddresses(IPConfigType ipv4_type, IPConfigType ipv6_type) {
    std::vector<net_base::IPCIDR> expected_result;
    if (ipv4_type != IPConfigType::kNone) {
      expected_result.push_back(*net_base::IPCIDR::CreateFromStringAndPrefix(
          GetIPPropertiesFromType(ipv4_type).address,
          GetIPPropertiesFromType(ipv4_type).subnet_prefix));
    }
    if (ipv6_type != IPConfigType::kNone) {
      expected_result.push_back(*net_base::IPCIDR::CreateFromStringAndPrefix(
          GetIPPropertiesFromType(ipv6_type).address,
          GetIPPropertiesFromType(ipv6_type).subnet_prefix));
    }

    EXPECT_EQ(network_->GetAddresses(), expected_result);
  }

  void VerifyIPTypeReportScheduled(Metrics::IPType type) {
    // Report should be triggered at T+30.
    dispatcher_.task_environment().FastForwardBy(base::Seconds(20));
    EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricIPType, _, type));
    dispatcher_.task_environment().FastForwardBy(base::Seconds(20));
  }

 private:
  const IPConfig::Properties* GetIPPropertiesPtrFromType(IPConfigType type) {
    switch (type) {
      case IPConfigType::kIPv4DHCP:
        return &ipv4_dhcp_props_;
      case IPConfigType::kIPv4Static:
        return &ipv4_static_props_;
      case IPConfigType::kIPv4LinkProtocol:
        return &ipv4_link_protocol_props_;
      case IPConfigType::kIPv4DHCPWithStatic:
        return &ipv4_dhcp_with_static_props_;
      case IPConfigType::kIPv4LinkProtocolWithStatic:
        return &ipv4_link_protocol_with_static_props_;
      case IPConfigType::kIPv6SLAAC:
        return &ipv6_slaac_props_;
      case IPConfigType::kIPv6LinkProtocol:
        return &ipv6_link_protocol_props_;
      default:
        return nullptr;
    }
  }

  IPConfig::Properties GetIPPropertiesFromType(IPConfigType type) {
    auto ptr = GetIPPropertiesPtrFromType(type);
    CHECK_NE(nullptr, ptr);
    return IPConfig::Properties(*ptr);
  }

  net_base::NetworkConfig ipv4_dhcp_config_;
  net_base::NetworkConfig ipv4_static_config_;
  net_base::NetworkConfig ipv4_link_protocol_config_;

  net_base::NetworkConfig slaac_config_;

  // IPConfig::Properties version of the above.
  IPConfig::Properties ipv4_dhcp_props_;
  IPConfig::Properties ipv4_static_props_;
  IPConfig::Properties ipv4_link_protocol_props_;

  IPConfig::Properties ipv4_dhcp_with_static_props_;
  IPConfig::Properties ipv4_link_protocol_with_static_props_;
  IPConfig::Properties ipv6_slaac_props_;
  IPConfig::Properties ipv6_link_protocol_props_;
};

TEST_F(NetworkStartTest, IPv4OnlyDHCPRequestIPFailure) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(network_->interface_index(),
                                               /*is_failure=*/true));
  EXPECT_CALL(event_handler2_, OnNetworkStopped(network_->interface_index(),
                                                /*is_failure=*/true));
  EXPECT_CALL(*network_, ApplyNetworkConfig).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kIdle);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPRequestIPFailureWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4Static, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPFailure) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(*network_, ApplyNetworkConfig).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler_, OnNetworkStopped(network_->interface_index(),
                                               /*is_failure=*/true));
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnNetworkStopped(network_->interface_index(),
                                                /*is_failure=*/true));
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kIdle);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPFailureWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4Static, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCP) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCP);
  EXPECT_CALL(event_handler_, OnGetDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPv4ConfiguredWithDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnGetDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPv4ConfiguredWithDHCPLease(network_->interface_index()));
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kNone);
  VerifyIPTypeReportScheduled(Metrics::kIPTypeIPv4Only);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCPWithStatic);
  // Still expect the DHCP lease callback in this case.
  EXPECT_CALL(event_handler_, OnGetDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPv4ConfiguredWithDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnGetDHCPLease(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPv4ConfiguredWithDHCPLease(network_->interface_index()));
  // Release DHCP should be called since we have static IP now.
  EXPECT_CALL(*dhcp_controller_,
              ReleaseIP(DHCPController::ReleaseReason::kStaticIP));
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCPWithStatic, IPConfigType::kNone);

  // Reset static IP, DHCP should be renewed.
  EXPECT_CALL(*dhcp_controller_, RenewIP());
  network_->OnStaticIPConfigChanged({});
}

TEST_F(NetworkStartTest, IPv4OnlyApplyStaticIPWhenDHCPConfiguring) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  // Nothing should happen if IP address is not set.
  net_base::NetworkConfig partial_config;
  partial_config.dns_servers = {
      *net_base::IPAddress::CreateFromString(kIPv4StaticNameServer)};
  network_->OnStaticIPConfigChanged(partial_config);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4Static);
  ConfigureStaticIPv4Config();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4Static, IPConfigType::kNone);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCPWithStatic);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCPWithStatic, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyApplyStaticIPAfterDHCPConnected) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCPWithStatic);
  ConfigureStaticIPv4Config();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCPWithStatic, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4LinkProtocol);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocol, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyLinkProtocolWithStaticIP) {
  const TestOptions test_opts = {
      .static_ipv4 = true,
      .link_protocol_ipv4 = true,
  };
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4LinkProtocolWithStatic);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocolWithStatic,
                  IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv6OnlySLAAC) {
  const TestOptions test_opts = {.accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);

  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_CALL(event_handler_, OnGetSLAACAddress(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPv6ConfiguredWithSLAACAddress(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnGetSLAACAddress(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPv6ConfiguredWithSLAACAddress(network_->interface_index()));
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kIPv6SLAAC);
  VerifyIPTypeReportScheduled(Metrics::kIPTypeIPv6Only);
}

TEST_F(NetworkStartTest, IPv6OnlySLAACAddressChangeEvent) {
  const TestOptions test_opts = {.accept_ra = true};
  InvokeStart(test_opts);
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(&network_applier_);

  // Changing the address should trigger the connection update.
  const auto new_addr =
      *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1234");
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(
                  ContainsAddressAndRoute(net_base::IPFamily::kIPv6), _));
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(NetworkApplier::Area::kRoutingPolicy, _));
  EXPECT_CALL(event_handler_, OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  TriggerSLAACAddressUpdate(net_base::IPv6CIDR(new_addr));
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // If the IPv6 address does not change, no signal is emitted.
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(NetworkApplier::Area::kRoutingPolicy, _));
  slaac_controller_->TriggerCallback(SLAACController::UpdateType::kAddress);
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // If the IPv6 prefix changes, a signal is emitted.
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(
                  ContainsAddressAndRoute(net_base::IPFamily::kIPv6), _));
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(NetworkApplier::Area::kRoutingPolicy, _));
  EXPECT_CALL(event_handler_, OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  TriggerSLAACAddressUpdate(
      *net_base::IPv6CIDR::CreateFromAddressAndPrefix(new_addr, 64));
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
}

TEST_F(NetworkStartTest, IPv6OnlySLAACDNSServerChangeEvent) {
  const TestOptions test_opts = {.accept_ra = true};
  InvokeStart(test_opts);

  // The Network should not be set up if there is no valid DNS.
  TriggerSLAACNameServersUpdate({});
  TriggerSLAACAddressUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  const auto dns_server =
      *net_base::IPAddress::CreateFromString(kIPv6SLAACNameserver);

  // A valid DNS should bring the network up.
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_CALL(event_handler_, OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  TriggerSLAACNameServersUpdate({dns_server});
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
  Mock::VerifyAndClearExpectations(&network_applier_);

  // If the IPv6 DNS server addresses does not change, no signal is emitted.
  TriggerSLAACNameServersUpdate({dns_server});
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Clear out the DNS server.
  EXPECT_CALL(event_handler_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  TriggerSLAACNameServersUpdate({});
  EXPECT_TRUE(network_->GetNetworkConfig().dns_servers.empty());
  Mock::VerifyAndClearExpectations(&event_handler2_);

  // Reset the DNS server.
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_CALL(event_handler_, OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnConnectionUpdated(network_->interface_index()));
  EXPECT_CALL(event_handler2_,
              OnIPConfigsPropertyUpdated(network_->interface_index()));
  TriggerSLAACNameServersUpdate({dns_server});
  EXPECT_EQ(network_->GetNetworkConfig().dns_servers.size(), 1);
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(&event_handler2_);
}

TEST_F(NetworkStartTest, IPv6OnlyLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv6 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6LinkProtocol);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kIPv6LinkProtocol);
  VerifyGetAddresses(IPConfigType::kNone, IPConfigType::kIPv6LinkProtocol);
}

TEST_F(NetworkStartTest, DualStackDHCPRequestIPFailure) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);
}

// Note that if the DHCP failure happens before we get the SLAAC address, the
// Network will be stopped.
TEST_F(NetworkStartTest, DualStackDHCPFailure) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(network_->interface_index(),
                                               /*is_failure=*/true));
  EXPECT_CALL(event_handler2_, OnNetworkStopped(network_->interface_index(),
                                                /*is_failure=*/true));

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure(network_->interface_index()));
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kIdle);
}

TEST_F(NetworkStartTest, DualStackDHCPFailureAfterIPv6Connected) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure(network_->interface_index()));
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure(network_->interface_index()));
  TriggerSLAACUpdate();
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
}

// Verifies the behavior on IPv4 failure after both v4 and v6 are connected.
TEST_F(NetworkStartTest, DualStackDHCPFailureAfterDHCPConnected) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  TriggerDHCPUpdateCallback();
  TriggerSLAACUpdate();

  // Connection should be reconfigured with IPv6 on IPv4 failure. Connection
  // should be reset.
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  TriggerDHCPFailureCallback();
  // TODO(b/232177767): We do not verify IPConfigs here, since currently we only
  // reset the properties in ipconfig on DHCP failure instead of removing it.
  // Consider changing this behavior in the future.
}

// When configuring if received DHCP option 108, continue to wait for SLAAC.
TEST_F(NetworkStartTest, RFC8925) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);
  TriggerDHCPOption108Callback();
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
}

TEST_F(NetworkStartTest, RFC8925IPv6ConnectedFirst) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure).Times(0);
  EXPECT_CALL(event_handler2_, OnGetDHCPFailure).Times(0);
  TriggerSLAACUpdate();
  TriggerDHCPOption108Callback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
}

// Verifies the behavior on option 108 after both v4 and v6 are connected.
TEST_F(NetworkStartTest, RFC8925Option108AfterIPv4Connected) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  TriggerDHCPUpdateCallback();
  TriggerSLAACUpdate();

  // Connection should be reconfigured with IPv6. Connection should be reset.
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  TriggerDHCPOption108Callback();
}

TEST_F(NetworkStartTest, DualStackSLAACFirst) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
  VerifyGetAddresses(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
}

TEST_F(NetworkStartTest, DualStackDHCPFirst) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  // Only routing policy and DNS will be updated when IPv6 config comes after
  // IPv4.
  EXPECT_CALL(*network_,
              ApplyNetworkConfig(NetworkApplier::Area::kRoutingPolicy, _));
  EXPECT_CALL(*network_, ApplyNetworkConfig(NetworkApplier::Area::kDNS, _));
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
  VerifyGetAddresses(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
  VerifyIPTypeReportScheduled(Metrics::kIPTypeDualStack);
}

// The dual-stack VPN case, Connection should be set up with IPv6 at first, and
// then IPv4.
TEST_F(NetworkStartTest, DualStackLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv4 = true,
                                 .link_protocol_ipv6 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped).Times(0);
  EXPECT_CALL(event_handler2_, OnNetworkStopped).Times(0);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6LinkProtocol);
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4LinkProtocol);

  InvokeStart(test_opts);

  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocol,
                  IPConfigType::kIPv6LinkProtocol);
  VerifyGetAddresses(IPConfigType::kIPv4LinkProtocol,
                     IPConfigType::kIPv6LinkProtocol);
}

// Verifies that the exposed IPConfig objects should be cleared on stopped.
TEST_F(NetworkStartTest, Stop) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  TriggerDHCPUpdateCallback();
  TriggerSLAACUpdate();

  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);

  EXPECT_CALL(event_handler_, OnNetworkStopped(network_->interface_index(), _));
  EXPECT_CALL(event_handler2_,
              OnNetworkStopped(network_->interface_index(), _));
  network_->Stop();
  EXPECT_EQ(network_->state(), Network::State::kIdle);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kNone);
}

// Verifies that 1) the handler set by RegisterCurrentIPConfigChangeHandler() is
// invoked properly, and 2) GetCurrentIPConfig returns the correct IPConfig
// object.
TEST_F(NetworkStartTest, CurrentIPConfigChangeHandler) {
  class MockHandler {
   public:
    MOCK_METHOD(void, OnCurrentIPChange, (), ());
  } handler;

  network_->RegisterCurrentIPConfigChangeHandler(base::BindRepeating(
      &MockHandler::OnCurrentIPChange, base::Unretained(&handler)));

  EXPECT_EQ(network_->GetCurrentIPConfig(), nullptr);

  // No trigger on nullptr -> nullptr
  EXPECT_CALL(handler, OnCurrentIPChange()).Times(0);
  network_->Stop();

  // Start the network.
  EXPECT_CALL(handler, OnCurrentIPChange()).Times(0);
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  // Trigger on nullptr -> ipv4.
  EXPECT_CALL(handler, OnCurrentIPChange());
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->GetCurrentIPConfig(), network_->ipconfig());
  Mock::VerifyAndClearExpectations(&handler);

  // No trigger on ipv4 -> ipv4
  EXPECT_CALL(handler, OnCurrentIPChange()).Times(0);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->GetCurrentIPConfig(), network_->ipconfig());
  Mock::VerifyAndClearExpectations(&handler);

  // Trigger on ipv4 -> ipv6.
  EXPECT_CALL(handler, OnCurrentIPChange());
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->GetCurrentIPConfig(), network_->ip6config());
  Mock::VerifyAndClearExpectations(&handler);

  // Trigger on ipv6 -> ipv4.
  EXPECT_CALL(handler, OnCurrentIPChange());
  ConfigureStaticIPv4Config();
  EXPECT_EQ(network_->GetCurrentIPConfig(), network_->ipconfig());
  Mock::VerifyAndClearExpectations(&handler);

  // Trigger on ipv4 -> nullptr.
  EXPECT_CALL(handler, OnCurrentIPChange());
  network_->Stop();
  EXPECT_EQ(network_->GetCurrentIPConfig(), nullptr);
}

TEST_F(NetworkStartTest, NoReportIPTypeForShortConnection) {
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricIPType, _, _)).Times(0);

  const TestOptions test_opts = {.dhcp = true};
  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  // Stop() should cancel the metric report task.
  network_->Stop();

  dispatcher_.task_environment().FastForwardBy(base::Minutes(1));
}

TEST(ValidationLogTest, ValidationLogRecordMetrics) {
  EventDispatcherForTest dispatcher;

  // Stub PortalDetector results:
  PortalDetector::Result i, r, p, n;

  // |i| -> kInternetConnectivity
  i.http_phase = PortalDetector::Phase::kContent;
  i.http_status = PortalDetector::Status::kSuccess;
  i.http_status_code = 204;
  i.http_content_length = 0;
  i.http_probe_completed = true;
  i.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            i.GetValidationState());

  // |r| -> kPortalRedirect
  r.http_phase = PortalDetector::Phase::kContent;
  r.http_status = PortalDetector::Status::kRedirect;
  r.http_status_code = 302;
  r.http_content_length = 0;
  r.https_error = HttpRequest::Error::kConnectionFailure;
  r.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  r.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  r.http_probe_completed = true;
  r.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            r.GetValidationState());

  // |p| -> kPortalSuspected
  p.http_phase = PortalDetector::Phase::kContent;
  p.http_status = PortalDetector::Status::kSuccess;
  p.http_status_code = 200;
  p.http_content_length = 678;
  p.https_error = HttpRequest::Error::kConnectionFailure;
  p.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  p.http_probe_completed = true;
  p.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            p.GetValidationState());

  // |n| -> kNoConnectivity
  n.http_phase = PortalDetector::Phase::kConnection;
  n.http_status = PortalDetector::Status::kFailure;
  n.https_error = HttpRequest::Error::kConnectionFailure;
  n.http_probe_completed = true;
  n.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            n.GetValidationState());

  struct {
    std::vector<PortalDetector::Result> results;
    Metrics::PortalDetectorAggregateResult expected_metric_enum;
  } test_cases[] = {
      {{i}, Metrics::kPortalDetectorAggregateResultInternet},
      {{i, r, p, n}, Metrics::kPortalDetectorAggregateResultInternet},
      {{r}, Metrics::kPortalDetectorAggregateResultRedirect},
      {{p}, Metrics::kPortalDetectorAggregateResultPartialConnectivity},
      {{n}, Metrics::kPortalDetectorAggregateResultNoConnectivity},
      {{n, n, n}, Metrics::kPortalDetectorAggregateResultNoConnectivity},
      {{n, n, p, n},
       Metrics::kPortalDetectorAggregateResultPartialConnectivity},
      {{p, r, r, p}, Metrics::kPortalDetectorAggregateResultRedirect},
      {{r, r, r, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterRedirect},
      {{r, p, r, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterRedirect},
      {{p, n, p, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterPartialConnectivity},
      {{p, p, i, i, r, r},
       Metrics::kPortalDetectorAggregateResultInternetAfterPartialConnectivity},
  };

  for (const auto& tt : test_cases) {
    StrictMock<MockMetrics> metrics;
    Network::ValidationLog log(Technology::kWiFi, &metrics);
    for (auto r : tt.results) {
      // Ensure that all durations between events are positive.
      dispatcher.task_environment().FastForwardBy(base::Milliseconds(10));
      log.AddResult(r);
    }

    EXPECT_CALL(metrics,
                SendEnumToUMA(Metrics::kPortalDetectorAggregateResult,
                              Technology::kWiFi, tt.expected_metric_enum));
    switch (tt.expected_metric_enum) {
      case Metrics::kPortalDetectorAggregateResultInternet:
      case Metrics::
          kPortalDetectorAggregateResultInternetAfterPartialConnectivity:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                       Technology::kWiFi, _));
        break;
      case Metrics::kPortalDetectorAggregateResultRedirect:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                       Technology::kWiFi, _));
        EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                           Metrics::kCapportNotSupported));
        break;
      case Metrics::kPortalDetectorAggregateResultInternetAfterRedirect:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                       Technology::kWiFi, _));
        EXPECT_CALL(
            metrics,
            SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect,
                      Technology::kWiFi, _));
        EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                           Metrics::kCapportNotSupported));
        break;
      case Metrics::kPortalDetectorAggregateResultNoConnectivity:
      case Metrics::kPortalDetectorAggregateResultPartialConnectivity:
      case Metrics::kPortalDetectorAggregateResultUnknown:
      default:
        EXPECT_CALL(metrics,
                    SendToUMA(Metrics::kPortalDetectorTimeToInternet, _, _))
            .Times(0);
        EXPECT_CALL(metrics,
                    SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
            .Times(0);
        EXPECT_CALL(
            metrics,
            SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect, _,
                      _))
            .Times(0);
        break;
    }

    log.RecordMetrics();
    Mock::VerifyAndClearExpectations(&metrics);
  }
}

TEST(ValidationLogTest, ValidationLogRecordMetricsWithoutRecord) {
  StrictMock<MockMetrics> metrics;

  EXPECT_CALL(metrics,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
      .Times(0);

  Network::ValidationLog log(Technology::kWiFi, &metrics);
  log.RecordMetrics();
}

TEST(ValidationLogTest, ValidationLogRecordMetricsCapportSupported) {
  PortalDetector::Result redirect_result;
  redirect_result.http_phase = PortalDetector::Phase::kContent;
  redirect_result.http_status = PortalDetector::Status::kRedirect;
  redirect_result.http_status_code = 302;
  redirect_result.http_content_length = 0;
  redirect_result.https_error = HttpRequest::Error::kConnectionFailure;
  redirect_result.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  redirect_result.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  redirect_result.http_probe_completed = true;
  redirect_result.https_probe_completed = true;

  MockMetrics metrics;
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                     Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                     Ne(Metrics::kCapportSupportedByDHCPv4)))
      .Times(0);

  Network::ValidationLog log(Technology::kWiFi, &metrics);
  log.AddResult(redirect_result);
  log.SetCapportDHCPSupported();
  log.RecordMetrics();
}

}  // namespace

}  // namespace
}  // namespace shill
