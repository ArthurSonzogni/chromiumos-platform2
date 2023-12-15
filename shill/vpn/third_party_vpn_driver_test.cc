// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/third_party_vpn_driver.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/memory/ptr_util.h>
#include <gtest/gtest.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>

#include "shill/callbacks.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_file_io.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/vpn_types.h"

using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace shill {

namespace {
constexpr char kInterfaceName[] = "tun0";
constexpr int kInterfaceIndex = 123;
}  // namespace

class ThirdPartyVpnDriverTest : public testing::Test {
 public:
  ThirdPartyVpnDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        driver_(new ThirdPartyVpnDriver(&manager_, nullptr)),
        adaptor_interface_(new ThirdPartyVpnMockAdaptor()) {}

  ~ThirdPartyVpnDriverTest() override = default;

  void SetUp() override {
    driver_->adaptor_interface_.reset(adaptor_interface_);
    driver_->file_io_ = &mock_file_io_;
  }

  void TearDown() override { driver_->file_io_ = nullptr; }

  MOCK_METHOD(void, TestCallback, (const Error&));

  MockDeviceInfo* device_info() { return manager_.mock_device_info(); }

 protected:
  void PrepareDriverForParameters() {
    ThirdPartyVpnDriver::active_client_ = driver_.get();
    driver_->parameters_expected_ = true;
  }

  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockFileIO mock_file_io_;
  MockManager manager_;
  MockVPNDriverEventHandler event_handler_;
  std::unique_ptr<ThirdPartyVpnDriver> driver_;
  ThirdPartyVpnMockAdaptor* adaptor_interface_;  // Owned by |driver_|
};

TEST_F(ThirdPartyVpnDriverTest, VPNType) {
  EXPECT_EQ(driver_->vpn_type(), VPNType::kThirdParty);
}

TEST_F(ThirdPartyVpnDriverTest, ConnectAndDisconnect) {
  const std::string interface = kInterfaceName;
  int fd = 1;

  DeviceInfo::LinkReadyCallback link_ready_callback;
  EXPECT_CALL(*device_info(), CreateTunnelInterface(_))
      .WillOnce([&link_ready_callback](DeviceInfo::LinkReadyCallback callback) {
        link_ready_callback = std::move(callback);
        return true;
      });
  driver_->ConnectAsync(&event_handler_);

  EXPECT_CALL(*device_info(), OpenTunnelInterface(interface))
      .WillOnce(Return(fd));
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kConnected)));
  std::move(link_ready_callback).Run(kInterfaceName, kInterfaceIndex);
  EXPECT_EQ(driver_->active_client_, driver_.get());
  EXPECT_TRUE(driver_->parameters_expected_);

  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kDisconnected)));
  EXPECT_CALL(mock_file_io_, Close(fd));
  driver_->Disconnect();
  EXPECT_FALSE(driver_->event_handler_);
}

TEST_F(ThirdPartyVpnDriverTest, ReconnectionEvents) {
  const std::string interface = kInterfaceName;
  int fd = 1;

  EXPECT_CALL(*device_info(), OpenTunnelInterface(interface))
      .WillOnce(Return(fd));
  EXPECT_CALL(*device_info(), CreateTunnelInterface(_)).WillOnce(Return(true));
  driver_->ConnectAsync(&event_handler_);
  driver_->OnLinkReady(kInterfaceName, kInterfaceIndex);

  driver_->reconnect_supported_ = true;

  // Roam from one Online network to another -> kLinkChanged.
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kLinkChanged)));
  driver_->OnDefaultPhysicalServiceEvent(
      VPNDriver::kDefaultPhysicalServiceChanged);

  // Default physical service is not Online -> kLinkDown.
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kLinkDown)));
  driver_->OnDefaultPhysicalServiceEvent(
      VPNDriver::kDefaultPhysicalServiceDown);

  // Default physical service comes Online -> kLinkUp.
  EXPECT_CALL(
      *adaptor_interface_,
      EmitPlatformMessage(static_cast<uint32_t>(ThirdPartyVpnDriver::kLinkUp)));
  driver_->OnDefaultPhysicalServiceEvent(VPNDriver::kDefaultPhysicalServiceUp);

  // Default physical service vanishes, but the app doesn't support
  // reconnecting -> kDisconnected.
  driver_->reconnect_supported_ = false;
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kDisconnected)));
  driver_->OnDefaultPhysicalServiceEvent(
      VPNDriver::kDefaultPhysicalServiceDown);

  driver_->Disconnect();
}

TEST_F(ThirdPartyVpnDriverTest, PowerEvents) {
  const std::string interface = kInterfaceName;
  int fd = 1;

  EXPECT_CALL(*device_info(), OpenTunnelInterface(interface))
      .WillOnce(Return(fd));
  EXPECT_CALL(*device_info(), CreateTunnelInterface(_)).WillOnce(Return(true));
  driver_->ConnectAsync(&event_handler_);
  driver_->OnLinkReady(kInterfaceName, kInterfaceIndex);

  driver_->reconnect_supported_ = true;

  ResultCallback callback = base::BindOnce(
      &ThirdPartyVpnDriverTest::TestCallback, base::Unretained(this));
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kSuspend)));
  EXPECT_CALL(*this, TestCallback(_));
  driver_->OnBeforeSuspend(std::move(callback));

  EXPECT_CALL(
      *adaptor_interface_,
      EmitPlatformMessage(static_cast<uint32_t>(ThirdPartyVpnDriver::kResume)));
  driver_->OnAfterResume();

  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kDisconnected)));
  driver_->Disconnect();
}

TEST_F(ThirdPartyVpnDriverTest, OnConnectTimeout) {
  EXPECT_CALL(*device_info(), CreateTunnelInterface(_)).WillOnce(Return(true));
  driver_->ConnectAsync(&event_handler_);

  EXPECT_CALL(event_handler_, OnDriverFailure(_, _));
  driver_->OnConnectTimeout();
  EXPECT_FALSE(driver_->event_handler_);
}

TEST_F(ThirdPartyVpnDriverTest, SendPacket) {
  int fd = 1;
  std::string error;
  std::vector<uint8_t> ip_packet(5, 0);
  driver_->SendPacket(ip_packet, &error);
  EXPECT_EQ(error, "Unexpected call");

  error.clear();
  ThirdPartyVpnDriver::active_client_ = driver_.get();
  driver_->SendPacket(ip_packet, &error);
  EXPECT_EQ(error, "Device not open");

  driver_->tun_fd_ = fd;
  error.clear();
  EXPECT_CALL(mock_file_io_, Write(fd, ip_packet.data(), ip_packet.size()))
      .WillOnce(Return(ip_packet.size() - 1));
  EXPECT_CALL(
      *adaptor_interface_,
      EmitPlatformMessage(static_cast<uint32_t>(ThirdPartyVpnDriver::kError)));
  driver_->SendPacket(ip_packet, &error);
  EXPECT_EQ(error, "Partial write");

  error.clear();
  EXPECT_CALL(mock_file_io_, Write(fd, ip_packet.data(), ip_packet.size()))
      .WillOnce(Return(ip_packet.size()));
  driver_->SendPacket(ip_packet, &error);
  EXPECT_TRUE(error.empty());

  driver_->tun_fd_ = -1;

  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kDisconnected)));
}

TEST_F(ThirdPartyVpnDriverTest, UpdateConnectionState) {
  std::string error;
  driver_->UpdateConnectionState(Service::kStateConfiguring, &error);
  EXPECT_EQ(error, "Unexpected call");

  error.clear();
  ThirdPartyVpnDriver::active_client_ = driver_.get();
  driver_->UpdateConnectionState(Service::kStateConfiguring, &error);
  EXPECT_EQ(error, "Invalid argument");

  error.clear();
  driver_->event_handler_ = &event_handler_;
  EXPECT_CALL(event_handler_, OnDriverFailure(_, _)).Times(0);
  driver_->UpdateConnectionState(Service::kStateOnline, &error);
  EXPECT_TRUE(error.empty());
  Mock::VerifyAndClearExpectations(&event_handler_);

  EXPECT_CALL(event_handler_, OnDriverFailure(_, _)).Times(1);
  EXPECT_CALL(*adaptor_interface_, EmitPlatformMessage(static_cast<uint32_t>(
                                       ThirdPartyVpnDriver::kDisconnected)))
      .Times(1);
  driver_->UpdateConnectionState(Service::kStateFailure, &error);
  EXPECT_TRUE(error.empty());
  Mock::VerifyAndClearExpectations(&event_handler_);
  Mock::VerifyAndClearExpectations(adaptor_interface_);
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersUnexpectedCall) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "Unexpected call");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersEmpty) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error,
            "address is missing;subnet_prefix is missing;"
            "exclusion_list is missing;inclusion_list is missing;");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersCorrect) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(driver_->network_config_->ipv4_address,
            net_base::IPv4CIDR::CreateFromCIDRString("123.211.21.18/12"));
  EXPECT_EQ(driver_->network_config_->excluded_route_prefixes.size(), 3);
  EXPECT_EQ(driver_->network_config_->excluded_route_prefixes[0],
            net_base::IPCIDR::CreateFromCIDRString("123.211.21.29/31"));
  EXPECT_EQ(driver_->network_config_->excluded_route_prefixes[1],
            net_base::IPCIDR::CreateFromCIDRString("0.0.0.0/0"));
  EXPECT_EQ(driver_->network_config_->excluded_route_prefixes[2],
            net_base::IPCIDR::CreateFromCIDRString("123.211.21.1/24"));
  EXPECT_EQ(driver_->network_config_->included_route_prefixes.size(), 2);
  EXPECT_EQ(driver_->network_config_->included_route_prefixes[0],
            net_base::IPCIDR::CreateFromCIDRString("123.211.61.29/7"));
  EXPECT_EQ(driver_->network_config_->included_route_prefixes[1],
            net_base::IPCIDR::CreateFromCIDRString("123.211.42.29/17"));
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersAddress) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "address is missing;");

  error.clear();
  parameters["address"] = "1234.1.1.1";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "address is not a valid IP;");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersSubnetPrefix) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "subnet_prefix is missing;");

  error.clear();
  parameters["subnet_prefix"] = "123";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "subnet_prefix not in expected range;");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersDNSServers) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  parameters["dns_servers"] = "12 123123 43902374 123.211.21.19";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(driver_->network_config_->dns_servers.size(), 1);
  EXPECT_EQ(driver_->network_config_->dns_servers[0],
            net_base::IPAddress::CreateFromString("123.211.21.19"));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(warning,
            "12 for dns_servers is invalid;"
            "123123 for dns_servers is invalid;"
            "43902374 for dns_servers is invalid;");
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersExclusionList) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  parameters["exclusion_list"] =
      "400.400.400.400/12 1.1.1.1/44 1.1.1.1/-1 "
      "123.211.21.0/23 123.211.21.1/23 123.211.21.0/25 "
      "1.1.1.1.1/12 1.1.1/13";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(warning,
            "400.400.400.400/12 for exclusion_list is invalid;"
            "1.1.1.1/44 for exclusion_list is invalid;"
            "1.1.1.1/-1 for exclusion_list is invalid;"
            "Duplicate entry for 123.211.21.1/23 in exclusion_list found;"
            "1.1.1.1.1/12 for exclusion_list is invalid;"
            "1.1.1/13 for exclusion_list is invalid;");

  warning.clear();
  parameters["exclusion_list"] = "0.0.0.0/0";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_TRUE(driver_->network_config_->excluded_route_prefixes.empty());
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersInclusionList) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";

  parameters["inclusion_list"] = "";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "inclusion_list has no valid values or is empty;");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersBroadcastAddress) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  parameters["broadcast_address"] = "abc";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "broadcast_address is not a valid IP;");
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersDomainSearch) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  parameters["domain_search"] = "";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "domain_search has no valid values or is empty;");
  EXPECT_TRUE(warning.empty());

  error.clear();
  parameters["domain_search"] = "google.com:google.com";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(driver_->network_config_->dns_search_domains.size(), 1);
  EXPECT_EQ(driver_->network_config_->dns_search_domains[0], "google.com");
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(warning.empty());
}

TEST_F(ThirdPartyVpnDriverTest, SetParametersReconnect) {
  std::map<std::string, std::string> parameters;
  std::string error;
  std::string warning;
  PrepareDriverForParameters();

  parameters["address"] = "123.211.21.18";
  parameters["subnet_prefix"] = "12";
  parameters["exclusion_list"] = "0.0.0.0/0 123.211.21.29/31 123.211.21.1/24";
  parameters["inclusion_list"] = "123.211.61.29/7 123.211.42.29/17";

  parameters["reconnect"] = "abc";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_EQ(error, "reconnect not a valid boolean;");
  EXPECT_TRUE(warning.empty());

  error.clear();
  parameters["reconnect"] = "true";
  driver_->SetParameters(parameters, &error, &warning);
  EXPECT_TRUE(driver_->reconnect_supported_);
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(warning.empty());
}

}  // namespace shill
