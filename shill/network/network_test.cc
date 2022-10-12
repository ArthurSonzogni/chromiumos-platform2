// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <optional>
#include <string>
#include <vector>

#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_routing_table.h"
#include "shill/net/ndisc.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/mock_dhcp_controller.h"
#include "shill/network/mock_dhcp_provider.h"
#include "shill/network/mock_network.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"

namespace shill {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Field;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::WithArg;

constexpr int kTestIfindex = 123;
constexpr char kTestIfname[] = "eth_test";
constexpr auto kTestTechnology = Technology::kUnknown;

// IPv4 properties from DHCP.
constexpr char kIPv4DHCPAddress[] = "192.168.1.2";
constexpr int kIPv4DHCPPrefix = 24;
constexpr char kIPv4DHCPGateway[] = "192.168.1.1";
constexpr char kIPv4DHCPNameServer[] = "192.168.1.3";
constexpr int kIPv4DHCPMTU = 1000;

// IPv4 properties from link protocol (e.g., VPN or Cellular).
constexpr char kIPv4LinkProtocolAddress[] = "192.168.3.2";
constexpr int kIPv4LinkProtocolPrefix = 24;
constexpr char kIPv4LinkProtocolGateway[] = "192.168.3.1";
constexpr char kIPv4LinkProtocolNameServer[] = "192.168.3.3";
constexpr int kIPv4LinkProtocolMTU = 1010;

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

NetworkConfig CreateIPv4NetworkConfig(
    const std::string& addr,
    int prefix_len,
    const std::string& gateway,
    const std::vector<std::string>& dns_servers,
    std::optional<int> mtu) {
  NetworkConfig config;
  config.ipv4_address_cidr =
      base::StringPrintf("%s/%d", addr.c_str(), prefix_len);
  config.ipv4_route = NetworkConfig::RouteProperties{
      .gateway = gateway,
  };
  config.dns_servers = dns_servers;
  config.mtu = mtu;
  return config;
}

// TODO(b/232177767): This function is IPv4-only currently. Implement the IPv6
// part when necessary.
IPConfig::Properties NetworkConfigToIPProperties(const NetworkConfig& config) {
  IPConfig::Properties props = {};
  props.address_family = IPAddress::kFamilyIPv4;
  props.UpdateFromNetworkConfig(config);
  return props;
}

// Allows us to fake/mock some functions in this test.
class NetworkInTest : public Network {
 public:
  NetworkInTest(int interface_index,
                const std::string& interface_name,
                Technology technology,
                bool fixed_ip_params,
                EventHandler* event_handler,
                ControlInterface* control_interface,
                DeviceInfo* device_info,
                EventDispatcher* dispatcher)
      : Network(interface_index,
                interface_name,
                technology,
                fixed_ip_params,
                event_handler,
                control_interface,
                device_info,
                dispatcher) {
    ON_CALL(*this, SetIPFlag(_, _, _)).WillByDefault(Return(true));
  }

  MOCK_METHOD(bool,
              SetIPFlag,
              (IPAddress::Family, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(std::unique_ptr<Connection>,
              CreateConnection,
              (),
              (const override));
};

class NetworkTest : public ::testing::Test {
 public:
  NetworkTest()
      : manager_(&control_interface_, &dispatcher_, nullptr),
        device_info_(&manager_) {
    network_ = std::make_unique<NiceMock<NetworkInTest>>(
        kTestIfindex, kTestIfname, kTestTechnology,
        /*fixed_ip_params=*/false, &event_handler_, &control_interface_,
        &device_info_, &dispatcher_);
    network_->set_dhcp_provider_for_testing(&dhcp_provider_);
    network_->set_routing_table_for_testing(&routing_table_);
    EXPECT_CALL(dhcp_provider_, CreateController(_, _, _)).Times(0);
    ON_CALL(*network_, CreateConnection()).WillByDefault([this]() {
      auto ret = std::make_unique<NiceMock<MockConnection>>(&device_info_);
      connection_ = ret.get();
      return ret;
    });
  }
  ~NetworkTest() override { network_ = nullptr; }

  // Expects calling CreateController() on DHCPProvider, and the following
  // RequestIP() call will return |request_ip_result|. The pointer to the
  // returned DHCPController will be stored in |dhcp_controller_|.
  void ExpectCreateDHCPController(bool request_ip_result) {
    EXPECT_CALL(dhcp_provider_, CreateController(_, _, _))
        .WillOnce(InvokeWithoutArgs([request_ip_result, this]() {
          auto controller = std::make_unique<NiceMock<MockDHCPController>>(
              &control_interface_, kTestIfname);
          EXPECT_CALL(*controller, RequestIP())
              .WillOnce(Return(request_ip_result));
          dhcp_controller_ = controller.get();
          return controller;
        }));
  }

 protected:
  // Order does matter in this group. See the constructor.
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;

  MockDHCPProvider dhcp_provider_;
  MockNetworkEventHandler event_handler_;
  NiceMock<MockRoutingTable> routing_table_;

  std::unique_ptr<NiceMock<NetworkInTest>> network_;

  // Variables owned by |network_|. Not guaranteed valid even if it's not null.
  MockDHCPController* dhcp_controller_ = nullptr;
  MockConnection* connection_ = nullptr;
};

TEST_F(NetworkTest, OnNetworkStoppedCalledOnStopAfterStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_, OnNetworkStopped(false)).Times(1);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);

  // Additional Stop() should not trigger the callback.
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStopWithoutStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  network_->Stop();
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

TEST_F(NetworkTest, OnNetworkStoppedCalledOnDHCPFailure) {
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_, OnNetworkStopped(true)).Times(1);
  ASSERT_NE(dhcp_controller_, nullptr);
  dhcp_controller_->TriggerFailureCallback();
}

TEST_F(NetworkTest, EnableARPFilteringOnStart) {
  ExpectCreateDHCPController(true);
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, "arp_announce", "2"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, "arp_ignore", "1"))
      .WillOnce(Return(true));
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

TEST_F(NetworkTest, EnableIPv6FlagsSLAAC) {
  // Not interested in IPv4 flags in this test.
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "disable_ipv6", "0"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "accept_dad", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "accept_ra", "2"))
      .WillOnce(Return(true));
  network_->Start(Network::StartOptions{.accept_ra = true});
}

TEST_F(NetworkTest, EnableIPv6FlagsLinkProtocol) {
  // Not interested in IPv4 flags in this test.
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "disable_ipv6", "0"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "accept_dad", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv6, "accept_ra", "2"))
      .WillOnce(Return(true));
  network_->set_link_protocol_ipv6_properties(
      std::make_unique<IPConfig::Properties>());
  network_->Start(Network::StartOptions{});
}

// Verifies that the DHCP options in Network::Start() is properly used when
// creating the DHCPController.
TEST_F(NetworkTest, DHCPOptions) {
  constexpr char kHostname[] = "hostname";
  constexpr char kLeaseName[] = "lease-name";

  ON_CALL(dhcp_provider_, CreateController(_, _, _))
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
  EXPECT_CALL(dhcp_provider_,
              CreateController(
                  _, Field(&DHCPProvider::Options::use_arp_gateway, false), _));
  NetworkConfig static_config;
  static_config.ipv4_address_cidr = "192.168.1.1/24";
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

    ipv6_slaac_props_.address_family = IPAddress::kFamilyIPv6;
    ipv6_slaac_props_.method = kTypeIPv6;
    ipv6_slaac_props_.address = kIPv6SLAACAddress;
    ipv6_slaac_props_.subnet_prefix = kIPv6SLAACPrefix;
    ipv6_slaac_props_.gateway = kIPv6SLAACGateway;
    ipv6_slaac_props_.dns_servers = {kIPv6SLAACNameserver};

    ipv6_link_protocol_props_.address_family = IPAddress::kFamilyIPv6;
    ipv6_link_protocol_props_.method = kTypeIPv6;
    ipv6_link_protocol_props_.address = kIPv6LinkProtocolAddress;
    ipv6_link_protocol_props_.subnet_prefix = kIPv6LinkProtocolPrefix;
    ipv6_link_protocol_props_.gateway = kIPv6LinkProtocolGateway;
    ipv6_link_protocol_props_.dns_servers = {kIPv6LinkProtocolNameserver};
  }

  void InvokeStart(const TestOptions& test_opts) {
    if (test_opts.static_ipv4) {
      ConfigureStaticIPv4Config();
    }
    if (test_opts.link_protocol_ipv4) {
      network_->set_link_protocol_ipv4_properties(
          std::make_unique<IPConfig::Properties>(ipv4_link_protocol_props_));
    }
    if (test_opts.link_protocol_ipv6) {
      network_->set_link_protocol_ipv6_properties(
          std::make_unique<IPConfig::Properties>(ipv6_link_protocol_props_));
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
    dhcp_controller_->TriggerFailureCallback();
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerDHCPUpdateCallback() {
    ASSERT_NE(dhcp_controller_, nullptr);
    dhcp_controller_->TriggerUpdateCallback(ipv4_dhcp_props_);
  }

  void TriggerSLAACUpdate() {
    TriggerSLAACNameServersUpdate({IPAddress(kIPv6SLAACNameserver)},
                                  ND_OPT_LIFETIME_INFINITY);
    TriggerSLAACAddressUpdate();
  }

  void TriggerSLAACAddressUpdate() {
    EXPECT_CALL(routing_table_, GetDefaultRouteFromKernel(kTestIfindex, _))
        .WillRepeatedly(WithArg<1>([](RoutingTableEntry* entry) {
          entry->gateway = IPAddress(kIPv6SLAACGateway);
          return true;
        }));
    IPAddress addr(kIPv6SLAACAddress, kIPv6SLAACPrefix);
    network_->OnIPv6AddressChanged(&addr);
    dispatcher_.task_environment().RunUntilIdle();
  }

  void TriggerSLAACNameServersUpdate(const std::vector<IPAddress>& dns_list,
                                     uint32_t lifetime) {
    EXPECT_CALL(device_info_, GetIPv6DnsServerAddresses(kTestIfindex, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(dns_list),
                              SetArgPointee<2>(lifetime), Return(true)));
    network_->OnIPv6DnsServerAddressesChanged();
    dispatcher_.task_environment().RunUntilIdle();
  }

  // Expect calling CreateConnection() on Network, followed by a
  // UpdateFromIPConfig() call on the created Connection object. These two
  // expectation need to be set at the same time since they are called together
  // in the source code, and here the MockConnection object is only created when
  // CreateConnection() is really called.
  void ExpectCreateConnectionWithIPConfig(IPConfigType ipconfig_type) {
    EXPECT_CALL(*network_, CreateConnection())
        .WillOnce([this, ipconfig_type]() {
          auto ret = std::make_unique<NiceMock<MockConnection>>(&device_info_);
          connection_ = ret.get();
          ExpectConnectionUpdateFromIPConfig(ipconfig_type);
          return ret;
        });
  }

  void ExpectConnectionUpdateFromIPConfig(IPConfigType ipconfig_type) {
    const auto expected_props = GetIPPropertiesFromType(ipconfig_type);
    EXPECT_CALL(*connection_, UpdateFromIPConfig(expected_props));
  }

  // Verifies the IPConfigs object exposed by Network is expected.
  void VerifyIPConfigs(IPConfigType ipv4_type, IPConfigType ipv6_type) {
    if (ipv4_type == IPConfigType::kNone) {
      EXPECT_EQ(network_->ipconfig(), nullptr);
    } else {
      ASSERT_NE(network_->ipconfig(), nullptr);
      EXPECT_EQ(network_->ipconfig()->properties(),
                GetIPPropertiesFromType(ipv4_type));
    }

    if (ipv6_type == IPConfigType::kNone) {
      EXPECT_EQ(network_->ip6config(), nullptr);
    } else {
      ASSERT_NE(network_->ip6config(), nullptr);
      EXPECT_EQ(network_->ip6config()->properties(),
                GetIPPropertiesFromType(ipv6_type));
    }
  }

 private:
  IPConfig::Properties GetIPPropertiesFromType(IPConfigType type) {
    switch (type) {
      case IPConfigType::kIPv4DHCP:
        return ipv4_dhcp_props_;
      case IPConfigType::kIPv4Static:
        return ipv4_static_props_;
      case IPConfigType::kIPv4LinkProtocol:
        return ipv4_link_protocol_props_;
      case IPConfigType::kIPv4DHCPWithStatic:
        return ipv4_dhcp_with_static_props_;
      case IPConfigType::kIPv4LinkProtocolWithStatic:
        return ipv4_link_protocol_with_static_props_;
      case IPConfigType::kIPv6SLAAC:
        return ipv6_slaac_props_;
      case IPConfigType::kIPv6LinkProtocol:
        return ipv6_link_protocol_props_;
      default:
        NOTREACHED();
    }
    return {};
  }

  NetworkConfig ipv4_dhcp_config_;
  NetworkConfig ipv4_static_config_;
  NetworkConfig ipv4_link_protocol_config_;

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
  EXPECT_CALL(event_handler_, OnNetworkStopped(/*is_failure=*/true));
  EXPECT_CALL(*network_, CreateConnection()).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kIdle);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPRequestIPFailureWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4Static, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPFailure) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(*network_, CreateConnection()).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure());
  EXPECT_CALL(event_handler_, OnNetworkStopped(/*is_failure=*/true));
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kIdle);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPFailureWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure());
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4Static, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCP) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4DHCP);
  EXPECT_CALL(event_handler_, OnGetDHCPLease());
  EXPECT_CALL(event_handler_, OnIPv4ConfiguredWithDHCPLease());
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyDHCPWithStaticIP) {
  const TestOptions test_opts = {.dhcp = true, .static_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4Static);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCPWithStatic);
  // Still expect the DHCP lease callback in this case.
  EXPECT_CALL(event_handler_, OnGetDHCPLease());
  EXPECT_CALL(event_handler_, OnIPv4ConfiguredWithDHCPLease());
  // Release DHCP should be called since we have static IP now.
  EXPECT_CALL(*dhcp_controller_,
              ReleaseIP(DHCPController::kReleaseReasonStaticIP));
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCPWithStatic, IPConfigType::kNone);

  // Reset static IP, DHCP should be renewed.
  EXPECT_CALL(*dhcp_controller_, RenewIP());
  network_->OnStaticIPConfigChanged({});
}

TEST_F(NetworkStartTest, IPv4OnlyApplyStaticIPWhenDHCPConfiguring) {
  const TestOptions test_opts = {.dhcp = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  // Nothing should happen if IP address is not set.
  NetworkConfig partial_config;
  partial_config.dns_servers = {kIPv4StaticNameServer};
  network_->OnStaticIPConfigChanged(partial_config);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4Static);
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
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCPWithStatic);
  ConfigureStaticIPv4Config();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4DHCPWithStatic, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv4 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4LinkProtocol);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocol, IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv4OnlyLinkProtocolWithStaticIP) {
  const TestOptions test_opts = {
      .static_ipv4 = true,
      .link_protocol_ipv4 = true,
  };
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4LinkProtocolWithStatic);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocolWithStatic,
                  IPConfigType::kNone);
}

TEST_F(NetworkStartTest, IPv6OnlySLAAC) {
  const TestOptions test_opts = {.accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  EXPECT_CALL(event_handler_, OnGetDHCPFailure()).Times(0);

  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_CALL(event_handler_, OnGetSLAACAddress());
  EXPECT_CALL(event_handler_, OnIPv6ConfiguredWithSLAACAddress());
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kIPv6SLAAC);
}

TEST_F(NetworkStartTest, IPv6OnlySLAACAddressChangeEvent) {
  const TestOptions test_opts = {.accept_ra = true};
  InvokeStart(test_opts);
  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv6SLAAC);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(connection_);

  // Note that the following code relies on the RoutingTable and DeviceInfo
  // setup in TriggerSLAACUpdate().
  ON_CALL(*connection_, IsIPv6()).WillByDefault(Return(true));

  // Changing the address should trigger the connection update.
  IPAddress new_addr("fe80::1aa9:5ff:abcd:1234");
  EXPECT_CALL(event_handler_, OnConnectionUpdated());
  EXPECT_CALL(event_handler_, OnIPConfigsPropertyUpdated());
  network_->OnIPv6AddressChanged(&new_addr);
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);

  // If the IPv6 address does not change, no signal is emitted.
  network_->OnIPv6AddressChanged(&new_addr);
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);

  // If the IPv6 prefix changes, a signal is emitted.
  new_addr.set_prefix(64);
  EXPECT_CALL(event_handler_, OnConnectionUpdated());
  EXPECT_CALL(event_handler_, OnIPConfigsPropertyUpdated());
  network_->OnIPv6AddressChanged(&new_addr);
  dispatcher_.task_environment().RunUntilIdle();
  Mock::VerifyAndClearExpectations(&event_handler_);
}

TEST_F(NetworkStartTest, IPv6OnlySLAACDNSServerChangeEvent) {
  const TestOptions test_opts = {.accept_ra = true};
  InvokeStart(test_opts);

  // The Network should not be set up if there is no valid DNS.
  TriggerSLAACNameServersUpdate({}, 0);
  TriggerSLAACAddressUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);

  const IPAddress dns_server(kIPv6SLAACNameserver);

  // A valid DNS should bring the network up.
  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_CALL(event_handler_, OnConnectionUpdated());
  EXPECT_CALL(event_handler_, OnIPConfigsPropertyUpdated());
  TriggerSLAACNameServersUpdate({dns_server}, 3600);
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(connection_);

  ON_CALL(*connection_, IsIPv6()).WillByDefault(Return(true));

  // If the IPv6 DNS server addresses does not change, no signal is emitted.
  TriggerSLAACNameServersUpdate({dns_server}, 3600);
  Mock::VerifyAndClearExpectations(&event_handler_);

  // Setting lifetime to 0 should expire and clear out the DNS server.
  EXPECT_CALL(event_handler_, OnIPConfigsPropertyUpdated());
  TriggerSLAACNameServersUpdate({dns_server}, 0);
  EXPECT_TRUE(network_->ip6config()->properties().dns_servers.empty());
  Mock::VerifyAndClearExpectations(&event_handler_);

  // Reset lifetime to 3600.
  EXPECT_CALL(event_handler_, OnConnectionUpdated());
  EXPECT_CALL(event_handler_, OnIPConfigsPropertyUpdated());
  TriggerSLAACNameServersUpdate({dns_server}, 3600);
  EXPECT_EQ(network_->ip6config()->properties().dns_servers.size(), 1);
  Mock::VerifyAndClearExpectations(&event_handler_);
}

TEST_F(NetworkStartTest, IPv6OnlyLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv6 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv6LinkProtocol);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kNone, IPConfigType::kIPv6LinkProtocol);
}

TEST_F(NetworkStartTest, DualStackDHCPRequestIPFailure) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/false);
  InvokeStart(test_opts);
  EXPECT_EQ(network_->state(), Network::State::kConfiguring);
}

// Note that if the DHCP failure happens before we get the SLAAC address, the
// Network will be stopped.
TEST_F(NetworkStartTest, DualStackDHCPFailure) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(/*is_failure=*/true));

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure());
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kIdle);
}

TEST_F(NetworkStartTest, DualStackDHCPFailureAfterIPv6Connected) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  EXPECT_CALL(event_handler_, OnGetDHCPFailure());
  TriggerSLAACUpdate();
  TriggerDHCPFailureCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);
}

// Verifies the behavior on IPv4 failure after both v4 and v6 are connected.
TEST_F(NetworkStartTest, DualStackDHCPFailureAfterDHCPConnected) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);
  TriggerDHCPUpdateCallback();
  TriggerSLAACUpdate();

  // Connection should be reconfigured with IPv6 on IPv4 failure.
  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6SLAAC);
  EXPECT_EQ(network_->state(), Network::State::kConnected);
  TriggerDHCPFailureCallback();
  // TODO(b/232177767): We do not verify IPConfigs here, since currently we only
  // reset the properties in ipconfig on DHCP failure instead of removing it.
  // Consider changing this behavior in the future.
}

TEST_F(NetworkStartTest, DualStackSLAACFirst) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv6SLAAC);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
}

TEST_F(NetworkStartTest, DualStackDHCPFirst) {
  const TestOptions test_opts = {.dhcp = true, .accept_ra = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  ExpectCreateDHCPController(/*request_ip_result=*/true);
  InvokeStart(test_opts);

  ExpectCreateConnectionWithIPConfig(IPConfigType::kIPv4DHCP);
  TriggerDHCPUpdateCallback();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  // This function will not be called when IPv6 config comes after IPv4.
  EXPECT_CALL(*connection_, UpdateFromIPConfig(_)).Times(0);
  TriggerSLAACUpdate();
  EXPECT_EQ(network_->state(), Network::State::kConnected);

  VerifyIPConfigs(IPConfigType::kIPv4DHCP, IPConfigType::kIPv6SLAAC);
}

// The dual-stack VPN case, Connection should be set up with IPv6 at first, and
// then IPv4.
TEST_F(NetworkStartTest, DualStackLinkProtocol) {
  const TestOptions test_opts = {.link_protocol_ipv4 = true,
                                 .link_protocol_ipv6 = true};
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);

  // Need to set two expectations in the lambda so cannot use
  // ExpectCreateConnectionWithIPConfig() directly.
  EXPECT_CALL(*network_, CreateConnection()).WillOnce([this]() {
    auto ret = std::make_unique<NiceMock<MockConnection>>(&device_info_);
    connection_ = ret.get();
    ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv6LinkProtocol);
    ExpectConnectionUpdateFromIPConfig(IPConfigType::kIPv4LinkProtocol);
    return ret;
  });

  InvokeStart(test_opts);

  EXPECT_EQ(network_->state(), Network::State::kConnected);
  VerifyIPConfigs(IPConfigType::kIPv4LinkProtocol,
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

  EXPECT_CALL(event_handler_, OnNetworkStopped(_));
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

}  // namespace

}  // namespace
}  // namespace shill
