// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/connection_diagnostics.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <gtest/gtest.h>

#include "shill/manager.h"
#include "shill/mock_dns_client.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/network/icmp_session.h"
#include "shill/network/mock_icmp_session.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SetArgPointee;
using testing::Test;

namespace shill {

namespace {
constexpr const char kInterfaceName[] = "int0";
constexpr const int kInterfaceIndex = 4;
constexpr net_base::IPAddress kIPv4DNSServer0(
    net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kIPv4DNSServer1(
    net_base::IPv4Address(8, 8, 4, 4));
constexpr net_base::IPAddress kIPv6DNSServer0(net_base::IPv6Address(
    0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88));
constexpr net_base::IPAddress kIPv6DNSServer1(net_base::IPv6Address(
    0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x44));
const std::vector<net_base::IPAddress> kIPv4DNSList{
    net_base::IPAddress(kIPv4DNSServer0),
    net_base::IPAddress(kIPv4DNSServer1),
};
const std::vector<net_base::IPAddress> kIPv6DNSList{
    net_base::IPAddress(kIPv6DNSServer0),
    net_base::IPAddress(kIPv6DNSServer1),
};
constexpr const char kHttpUrl[] = "http://www.gstatic.com/generate_204";
const auto kIPv6DeviceAddress =
    *net_base::IPAddress::CreateFromString("2001:db8::3333:4444:5555");
const auto kIPv4ServerAddress =
    *net_base::IPAddress::CreateFromString("8.8.8.8");
const auto kIPv6ServerAddress =
    *net_base::IPAddress::CreateFromString("fe80::1aa9:5ff:7ebf:14c5");
const auto kIPv4GatewayAddress =
    *net_base::IPAddress::CreateFromString("192.168.1.1");
const auto kIPv6GatewayAddress =
    *net_base::IPAddress::CreateFromString("fee2::11b2:53f:13be:125e");
const std::vector<base::TimeDelta> kEmptyResult;
const std::vector<base::TimeDelta> kNonEmptyResult{base::Milliseconds(10)};
}  // namespace

class ConnectionDiagnosticsTest : public Test {
 public:
  ConnectionDiagnosticsTest()
      : gateway_(kIPv4GatewayAddress),
        dns_list_({kIPv4DNSServer0, kIPv4DNSServer1}),
        connection_diagnostics_(kInterfaceName,
                                kInterfaceIndex,
                                net_base::IPFamily::kIPv4,
                                kIPv4GatewayAddress,
                                {kIPv4DNSServer0, kIPv4DNSServer1},
                                "int0 mock_service sid=0",
                                &dispatcher_) {}

  ~ConnectionDiagnosticsTest() override = default;

  void SetUp() override {
    ASSERT_EQ(net_base::IPFamily::kIPv4, kIPv4ServerAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv4, kIPv4GatewayAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv6, kIPv6ServerAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv6, kIPv6GatewayAddress.GetFamily());

    dns_client_ = new NiceMock<MockDnsClient>();
    icmp_session_ = new NiceMock<MockIcmpSession>(&dispatcher_);
    connection_diagnostics_.dns_client_.reset(dns_client_);  // Passes ownership
    connection_diagnostics_.icmp_session_.reset(
        icmp_session_);  // Passes ownership
  }

  void TearDown() override {}

 protected:
  net_base::IPAddress gateway() { return gateway_; }

  void SetDNS(const std::vector<net_base::IPAddress>& dns) {
    dns_list_ = dns;
    connection_diagnostics_.dns_list_ = dns_list_;
  }

  void UseIPv6() {
    gateway_ = kIPv6GatewayAddress;
    connection_diagnostics_.ip_family_ = net_base::IPFamily::kIPv6,
    connection_diagnostics_.gateway_ = gateway_;
    SetDNS({kIPv6DNSServer0, kIPv6DNSServer1});
  }

  bool Start(const std::string& url) {
    return connection_diagnostics_.Start(
        *net_base::HttpUrl::CreateFromString(url));
  }

  void VerifyStopped() {
    EXPECT_FALSE(connection_diagnostics_.IsRunning());
    EXPECT_EQ(0, connection_diagnostics_.num_dns_attempts_);
    EXPECT_EQ(0, connection_diagnostics_.event_number());
    EXPECT_EQ(nullptr, connection_diagnostics_.dns_client_);
    EXPECT_FALSE(connection_diagnostics_.icmp_session_->IsStarted());
    EXPECT_TRUE(
        connection_diagnostics_.id_to_pending_dns_server_icmp_session_.empty());
    EXPECT_EQ(std::nullopt, connection_diagnostics_.target_url_);
  }

  void ExpectIcmpSessionStop() { EXPECT_CALL(*icmp_session_, Stop()); }

  void ExpectSuccessfulStart() {
    EXPECT_FALSE(connection_diagnostics_.IsRunning());
    EXPECT_EQ(0, connection_diagnostics_.event_number());
    EXPECT_TRUE(Start(kHttpUrl));
    EXPECT_TRUE(connection_diagnostics_.IsRunning());
  }

  void ExpectPingDNSServersStartSuccess(
      const std::vector<net_base::IPAddress>& dns = kIPv4DNSList) {
    ExpectPingDNSSeversStart(dns, /*is_success=*/true);
  }

  void ExpectPingDNSSeversStartFailureAllIcmpSessionsFailed(
      const std::vector<net_base::IPAddress>& dns = kIPv4DNSList) {
    ExpectPingDNSSeversStart(dns, /*is_success=*/false);
  }

  void ExpectPingDNSServersEndSuccessRetriesLeft() {
    ExpectPingDNSServersEndSuccess(true);
  }

  void ExpectPingDNSServersEndSuccessNoRetriesLeft() {
    ExpectPingDNSServersEndSuccess(false);
  }

  void ExpectPingDNSServersEndFailure() {
    // Post task to find DNS server route only after all (i.e. 2) pings are
    // done.
    connection_diagnostics_.OnPingDNSServerComplete(0, kEmptyResult);
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
    connection_diagnostics_.OnPingDNSServerComplete(1, kEmptyResult);
  }

  void ExpectResolveTargetServerIPAddressStartSuccess() {
    std::vector<std::string> pingable_dns_servers;
    for (const auto& dns : dns_list_) {
      pingable_dns_servers.push_back(dns.ToString());
    }
    EXPECT_CALL(*dns_client_,
                Start(pingable_dns_servers,
                      connection_diagnostics_.target_url_->host(), _))
        .WillOnce(Return(true));
    connection_diagnostics_.ResolveTargetServerIPAddress(dns_list_);
  }

  void ExpectResolveTargetServerIPAddressEndSuccess(
      const net_base::IPAddress& resolved_address) {
    ExpectResolveTargetServerIPAddressEnd(
        ConnectionDiagnostics::Result::kSuccess, resolved_address);
  }

  void ExpectResolveTargetServerIPAddressEndTimeout() {
    ExpectResolveTargetServerIPAddressEnd(
        ConnectionDiagnostics::Result::kTimeout,
        net_base::IPAddress(net_base::IPFamily::kIPv4));
  }

  void ExpectResolveTargetServerIPAddressEndFailure() {
    ExpectResolveTargetServerIPAddressEnd(
        ConnectionDiagnostics::Result::kFailure,
        net_base::IPAddress(net_base::IPFamily::kIPv4));
  }

  void ExpectPingHostStartSuccess(ConnectionDiagnostics::Type ping_event_type,
                                  const net_base::IPAddress& address) {
    EXPECT_CALL(*icmp_session_,
                Start(address, kInterfaceIndex, kInterfaceName, _))
        .WillOnce(Return(true));
    connection_diagnostics_.PingHost(
        ConnectionDiagnostics::Type::kPingTargetServer, address);
  }

  void ExpectPingHostStartFailure(ConnectionDiagnostics::Type ping_event_type,
                                  const net_base::IPAddress& address) {
    EXPECT_CALL(*icmp_session_,
                Start(address, kInterfaceIndex, kInterfaceName, _))
        .WillOnce(Return(false));
    connection_diagnostics_.PingHost(
        ConnectionDiagnostics::Type::kPingTargetServer, address);
  }

  void ExpectPingHostEndSuccess(ConnectionDiagnostics::Type ping_event_type,
                                const net_base::IPAddress& address) {
    connection_diagnostics_.OnPingHostComplete(ping_event_type, address,
                                               kNonEmptyResult);
  }

  void ExpectPingHostEndFailure(ConnectionDiagnostics::Type ping_event_type,
                                const net_base::IPAddress& address) {
    connection_diagnostics_.OnPingHostComplete(ping_event_type, address,
                                               kEmptyResult);
  }

 private:
  void ExpectPingDNSSeversStart(
      const std::vector<net_base::IPAddress>& expected_dns, bool is_success) {
    if (is_success) {
      connection_diagnostics_.id_to_pending_dns_server_icmp_session_.clear();
      for (size_t i = 0; i < expected_dns.size(); i++) {
        auto dns_server_icmp_session =
            std::make_unique<NiceMock<MockIcmpSession>>(&dispatcher_);
        EXPECT_CALL(*dns_server_icmp_session,
                    Start(expected_dns[i], kInterfaceIndex, kInterfaceName, _))
            .WillOnce(Return(is_success));
        connection_diagnostics_.id_to_pending_dns_server_icmp_session_[i] =
            std::move(dns_server_icmp_session);
      }
    }

    connection_diagnostics_.PingDNSServers();
    if (is_success) {
      EXPECT_EQ(expected_dns.size(),
                connection_diagnostics_.id_to_pending_dns_server_icmp_session_
                    .size());
    } else {
      EXPECT_TRUE(connection_diagnostics_.id_to_pending_dns_server_icmp_session_
                      .empty());
    }
  }

  void ExpectResolveTargetServerIPAddressEnd(
      ConnectionDiagnostics::Result result,
      const net_base::IPAddress& resolved_address) {
    Error error;
    if (result == ConnectionDiagnostics::Result::kSuccess) {
      error.Populate(Error::kSuccess);
      EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
    } else if (result == ConnectionDiagnostics::Result::kTimeout) {
      error.Populate(Error::kOperationTimeout);
      EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
    } else {
      error.Populate(Error::kOperationFailed);
    }
    if (error.IsSuccess()) {
      connection_diagnostics_.OnDNSResolutionComplete(resolved_address);
    } else {
      connection_diagnostics_.OnDNSResolutionComplete(base::unexpected(error));
    }
  }

  void ExpectPingDNSServersEndSuccess(bool retries_left) {
    if (retries_left) {
      EXPECT_LT(connection_diagnostics_.num_dns_attempts_,
                ConnectionDiagnostics::kMaxDNSRetries);
    } else {
      EXPECT_GE(connection_diagnostics_.num_dns_attempts_,
                ConnectionDiagnostics::kMaxDNSRetries);
    }
    // Post retry task or report done only after all (i.e. 2) pings are done.
    connection_diagnostics_.OnPingDNSServerComplete(0, kNonEmptyResult);
    if (retries_left) {
      EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
    } else {
      EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()))
          .Times(0);
    }
    connection_diagnostics_.OnPingDNSServerComplete(1, kNonEmptyResult);
  }

  net_base::IPAddress gateway_;
  std::vector<net_base::IPAddress> dns_list_;
  ConnectionDiagnostics connection_diagnostics_;
  NiceMock<MockEventDispatcher> dispatcher_;

  // Used only for EXPECT_CALL(). Objects are owned by
  // |connection_diagnostics_|.
  NiceMock<MockDnsClient>* dns_client_;
  NiceMock<MockIcmpSession>* icmp_session_;
};

TEST_F(ConnectionDiagnosticsTest, EndWith_InternalError) {
  // DNS resolution succeeds, and we attempt to ping the target web server but
  // fail because of an internal error.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_DNSFailure) {
  // DNS resolution fails (not timeout), so we end diagnostics.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndFailure();
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingDNSServerStartFailure_1) {
  // we attempt to pinging DNS servers, but fail to start any IcmpSessions, so
  // end diagnostics.
  ExpectSuccessfulStart();
  ExpectPingDNSSeversStartFailureAllIcmpSessionsFailed();
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingDNSServerEndSuccess_NoRetries_1) {
  // Pinging DNS servers succeeds, DNS resolution times out, pinging DNS servers
  // succeeds again, and DNS resolution times out again. End diagnostics because
  // we have no more DNS retries left.
  ExpectSuccessfulStart();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessNoRetriesLeft();
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingDNSServerEndSuccess_NoRetries_2) {
  // DNS resolution times out, pinging DNS servers succeeds, DNS resolution
  // times out again, pinging DNS servers succeeds. End diagnostics because we
  // have no more DNS retries left.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessNoRetriesLeft();
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetIPSuccess_1) {
  // DNS resolution succeeds, and pinging the resolved IP address succeeds, so
  // we end diagnostics.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetIPSuccess_2) {
  // pinging DNS servers succeeds, DNS resolution succeeds, and pinging the
  // resolved IP address succeeds, so we end diagnostics.
  ExpectSuccessfulStart();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetIPSuccess_3) {
  // DNS resolution times out, pinging DNS servers succeeds, DNS resolution
  // succeeds, and pinging the resolved IP address succeeds, so we end
  // diagnostics.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_1_IPv4) {
  // DNS resolution succeeds, pinging the resolved IP address fails, the
  // diagostics ends.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_1_IPv6) {
  // Same as above, but this time the resolved IP address of the target URL is
  // IPv6.
  UseIPv6();

  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv6ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv6ServerAddress);
  ExpectPingHostEndFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv6ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_2) {
  // Pinging DNS servers succeeds, DNS resolution succeeds, pinging the resolved
  // IP address fails, the diagnostics ends.
  ExpectSuccessfulStart();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostEndFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_3) {
  // DNS resolution times out, pinging DNS servers succeeds, DNS resolution
  // succeeds, pinging the resolved IP address fails, the diagnostics ends.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndTimeout();
  ExpectPingDNSServersStartSuccess();
  ExpectPingDNSServersEndSuccessRetriesLeft();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingGatewayFailure) {
  // DNS resolution succeeds, pinging the resolved IP address fails, the
  // diagnostics ends.
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStartSuccess();
  ExpectResolveTargetServerIPAddressEndSuccess(kIPv4ServerAddress);
  ExpectPingHostStartSuccess(ConnectionDiagnostics::Type::kPingTargetServer,
                             kIPv4ServerAddress);
  ExpectPingHostEndFailure(ConnectionDiagnostics::Type::kPingTargetServer,
                           kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, DualStackDNSPingFiltersIPFamily) {
  // Configure DNS with a mix of IPv4 and IPv6 addresses.
  std::vector<net_base::IPAddress> dns;
  dns.insert(dns.end(), kIPv4DNSList.begin(), kIPv4DNSList.end());
  dns.insert(dns.end(), kIPv6DNSList.begin(), kIPv6DNSList.end());
  SetDNS(dns);

  // If connection diagnostics runs for IPv4, only IPv4 DNS servers should be
  // pinged.
  ExpectSuccessfulStart();
  ExpectPingDNSServersStartSuccess(kIPv4DNSList);
}
}  // namespace shill
