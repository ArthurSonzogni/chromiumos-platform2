// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/connection_diagnostics.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <gtest/gtest.h>

#include "shill/manager.h"
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
constexpr const int kDiagnosticId = 1;
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

MATCHER_P(OptsEq, opt, "") {
  return arg.number_of_tries == opt.number_of_tries &&
         arg.interface == opt.interface && arg.name_server == opt.name_server;
}

class FakeDNSClient : public net_base::DNSClient {
 public:
  FakeDNSClient(std::string_view hostname,
                std::optional<net_base::IPAddress> dns)
      : hostname_(hostname), dns_(dns) {}
  std::string_view hostname() { return hostname_; }
  std::optional<net_base::IPAddress> dns() { return dns_; }

 private:
  std::string hostname_;
  std::optional<net_base::IPAddress> dns_;
};

class FakeDNSClientFactory : public net_base::DNSClientFactory {
 public:
  using Callbacks = std::vector<net_base::DNSClient::Callback>;

  FakeDNSClientFactory() {
    ON_CALL(*this, Resolve)
        .WillByDefault([](net_base::IPFamily family, std::string_view hostname,
                          net_base::DNSClient::CallbackWithDuration callback,
                          const net_base::DNSClient::Options& options,
                          net_base::AresInterface* ares_interface) {
          return std::make_unique<FakeDNSClient>(hostname, options.name_server);
        });
  }

  MOCK_METHOD(std::unique_ptr<net_base::DNSClient>,
              Resolve,
              (net_base::IPFamily family,
               std::string_view hostname,
               net_base::DNSClient::CallbackWithDuration callback,
               const net_base::DNSClient::Options& options,
               net_base::AresInterface* ares_interface),
              (override));
};

class FakeIcmpSessionFactory : public IcmpSessionFactory {
 public:
  FakeIcmpSessionFactory() : IcmpSessionFactory() {}

  std::unique_ptr<IcmpSession> SendPingRequest(
      const net_base::IPAddress& destination,
      int interface_index,
      std::string_view interface_name,
      std::string_view logging_tag,
      IcmpSession::IcmpSessionResultCallback result_callback,
      EventDispatcher* dispatcher,
      std::unique_ptr<net_base::SocketFactory> socket_factory) override {
    auto it = mock_icmp_sessions_.find(destination);
    return (it != mock_icmp_sessions_.end()) ? std::move(it->second) : nullptr;
  }

  void SetIcmpSession(const net_base::IPAddress& destination) {
    mock_icmp_sessions_[destination] =
        std::make_unique<NiceMock<MockIcmpSession>>();
  }

  std::map<net_base::IPAddress, std::unique_ptr<MockIcmpSession>>
      mock_icmp_sessions_;
};

}  // namespace

class ConnectionDiagnosticsTest : public Test {
 public:
  ConnectionDiagnosticsTest()
      : gateway_(kIPv4GatewayAddress),
        dns_list_({kIPv4DNSServer0, kIPv4DNSServer1}),
        dns_client_factory_(new FakeDNSClientFactory()),
        icmp_session_factory_(new FakeIcmpSessionFactory()),
        connection_diagnostics_(kInterfaceName,
                                kInterfaceIndex,
                                net_base::IPFamily::kIPv4,
                                kIPv4GatewayAddress,
                                {kIPv4DNSServer0, kIPv4DNSServer1},
                                base::WrapUnique(dns_client_factory_),
                                base::WrapUnique(icmp_session_factory_),
                                "int0 mock_service sid=0",
                                &dispatcher_) {}

  ~ConnectionDiagnosticsTest() override = default;

  void SetUp() override {
    ASSERT_EQ(net_base::IPFamily::kIPv4, kIPv4ServerAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv4, kIPv4GatewayAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv6, kIPv6ServerAddress.GetFamily());
    ASSERT_EQ(net_base::IPFamily::kIPv6, kIPv6GatewayAddress.GetFamily());
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

  void VerifyStopped() {
    EXPECT_FALSE(connection_diagnostics_.IsRunning());
    EXPECT_TRUE(connection_diagnostics_.dns_queries_.empty());
    EXPECT_TRUE(connection_diagnostics_.host_icmp_sessions_.empty());
    EXPECT_TRUE(
        connection_diagnostics_.id_to_pending_dns_server_icmp_session_.empty());
  }

  // Starts the ConnectionDiagnostics instance under test. If the given URL is
  // a valid URL string, is is passed to the ConnectionDiagnostics instance when
  // started.
  void ExpectSuccessfulStart(std::string url = kHttpUrl) {
    EXPECT_FALSE(connection_diagnostics_.IsRunning());
    connection_diagnostics_.Start(net_base::HttpUrl::CreateFromString(url));
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

  void ExpectPingDNSServersEndFailure() {
    // Post task to find DNS server route only after all (i.e. 2) pings are
    // done.
    connection_diagnostics_.OnPingDNSServerComplete(kDiagnosticId, 0,
                                                    kEmptyResult);
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
    connection_diagnostics_.OnPingDNSServerComplete(kDiagnosticId, 1,
                                                    kEmptyResult);
  }

  void ExpectResolveTargetServerIPAddressStart() {
    std::vector<std::string> pingable_dns_servers;
    for (const auto& dns : dns_list_) {
      pingable_dns_servers.push_back(dns.ToString());
    }
    net_base::HttpUrl url = *net_base::HttpUrl::CreateFromString(kHttpUrl);
    for (const auto& dns : dns_list_) {
      net_base::DNSClient::Options opts = {
          .number_of_tries = 2,
          .interface = kInterfaceName,
          .name_server = dns,
      };
      EXPECT_CALL(
          *dns_client_factory_,
          Resolve(gateway_.GetFamily(), url.host(), _, OptsEq(opts), _));
    }
    connection_diagnostics_.ResolveHostIPAddress(url, dns_list_);
  }

  void TriggerResolveTargetServerIPAddressEndSuccess(
      const net_base::IPAddress& dns_server,
      const std::vector<net_base::IPAddress>& resolved_addresses,
      bool simulate_icmp_failure = false) {
    if (!simulate_icmp_failure) {
      for (const auto& addr : resolved_addresses) {
        icmp_session_factory_->SetIcmpSession(addr);
      }
    }
    connection_diagnostics_.OnHostResolutionComplete(
        kDiagnosticId, ConnectionDiagnostics::Type::kResolveTargetServerIP,
        dns_server, resolved_addresses);
  }

  void TriggerResolveTargetServerIPAddressEndFailure(
      const net_base::IPAddress& dns_server) {
    connection_diagnostics_.OnHostResolutionComplete(
        kDiagnosticId, ConnectionDiagnostics::Type::kResolveTargetServerIP,
        dns_server, base::unexpected(net_base::DNSClient::Error::kTimedOut));
  }

  void TriggerPingHostEndSuccess(const net_base::IPAddress& address) {
    connection_diagnostics_.OnPingHostComplete(kDiagnosticId, address,
                                               kNonEmptyResult);
  }

  void TriggerPingHostEndFailure(const net_base::IPAddress& address) {
    connection_diagnostics_.OnPingHostComplete(kDiagnosticId, address,
                                               kEmptyResult);
  }

  void TriggerPingDNSServersEndSuccess() {
    // Post retry task or report done only after all (i.e. 2) pings are done.
    connection_diagnostics_.OnPingDNSServerComplete(kDiagnosticId, 0,
                                                    kNonEmptyResult);
    connection_diagnostics_.OnPingDNSServerComplete(kDiagnosticId, 1,
                                                    kNonEmptyResult);
  }

  void TriggerPingGatewayStartSuccess(const net_base::IPAddress& address) {
    icmp_session_factory_->SetIcmpSession(address);
    connection_diagnostics_.PingGateway(kDiagnosticId);
  }

  void TriggerPingGatewayStartFailure(const net_base::IPAddress& address) {
    connection_diagnostics_.PingGateway(kDiagnosticId);
  }

  void TriggerPingGatewayEndSuccess() {
    connection_diagnostics_.OnPingGatewayComplete(kDiagnosticId,
                                                  kNonEmptyResult);
  }

  void TriggerPingGatewayEndFailure() {
    connection_diagnostics_.OnPingGatewayComplete(kDiagnosticId, kEmptyResult);
  }

 private:
  void ExpectPingDNSSeversStart(
      const std::vector<net_base::IPAddress>& expected_dns, bool is_success) {
    if (is_success) {
      for (size_t i = 0; i < expected_dns.size(); i++) {
        icmp_session_factory_->SetIcmpSession(expected_dns[i]);
      }
    }

    connection_diagnostics_.PingDNSServers(kDiagnosticId);
    if (is_success) {
      EXPECT_EQ(expected_dns.size(),
                connection_diagnostics_.id_to_pending_dns_server_icmp_session_
                    .size());
    } else {
      EXPECT_TRUE(connection_diagnostics_.id_to_pending_dns_server_icmp_session_
                      .empty());
    }
  }

  net_base::IPAddress gateway_;
  std::vector<net_base::IPAddress> dns_list_;
  FakeDNSClientFactory* dns_client_factory_;
  FakeIcmpSessionFactory* icmp_session_factory_;
  ConnectionDiagnostics connection_diagnostics_;
  NiceMock<MockEventDispatcher> dispatcher_;
};

TEST_F(ConnectionDiagnosticsTest, EndWith_DNSICMPError) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(
      kIPv4DNSServer0, {kIPv4ServerAddress}, /*simulate_icmp_failure=*/true);
  TriggerResolveTargetServerIPAddressEndSuccess(
      kIPv4DNSServer1, {kIPv4ServerAddress}, /*simulate_icmp_failure=*/true);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_GatewayICMPStartError) {
  // DNS resolution succeeds, and we attempt to ping the target web server but
  // fail because of an internal error.
  ExpectSuccessfulStart();
  TriggerPingGatewayStartFailure(kIPv4GatewayAddress);
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_DNSFailure) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndFailure(kIPv4DNSServer0);
  TriggerResolveTargetServerIPAddressEndFailure(kIPv4DNSServer1);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingDNSServerStartFailure) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSSeversStartFailureAllIcmpSessionsFailed();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingDNSServerEndSuccess_NoRetries_1) {
  // Pinging DNS servers succeeds, DNS resolution times out, the diagnostics
  // ends.
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndFailure(kIPv4DNSServer0);
  TriggerResolveTargetServerIPAddressEndFailure(kIPv4DNSServer1);
  TriggerPingHostEndFailure(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_Success1) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_Success2) {
  ExpectSuccessfulStart();
  ExpectPingDNSServersStartSuccess();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingGatewayEndSuccess();
  TriggerPingDNSServersEndSuccess();
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_Success3) {
  ExpectSuccessfulStart();
  ExpectResolveTargetServerIPAddressStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  ExpectPingDNSServersStartSuccess();
  TriggerPingGatewayEndSuccess();
  TriggerPingDNSServersEndSuccess();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_SuccessIPv6) {
  UseIPv6();
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv6GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess({kIPv6DNSServer0, kIPv6DNSServer1});
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv6DNSServer0,
                                                {kIPv6ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv6DNSServer1,
                                                {kIPv6ServerAddress});
  TriggerPingHostEndSuccess(kIPv6ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_1_IPv4) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndFailure(kIPv4ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingTargetFailure_1_IPv6) {
  UseIPv6();

  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv6GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess({kIPv6DNSServer0, kIPv6DNSServer1});
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv6DNSServer0,
                                                {kIPv6ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv6DNSServer1,
                                                {kIPv6ServerAddress});
  TriggerPingHostEndFailure(kIPv6ServerAddress);
  VerifyStopped();
}

TEST_F(ConnectionDiagnosticsTest, EndWith_PingGatewayFailure) {
  ExpectSuccessfulStart();
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndFailure();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  ExpectResolveTargetServerIPAddressStart();
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer0,
                                                {kIPv4ServerAddress});
  TriggerResolveTargetServerIPAddressEndSuccess(kIPv4DNSServer1,
                                                {kIPv4ServerAddress});
  TriggerPingHostEndSuccess(kIPv4ServerAddress);
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

TEST_F(ConnectionDiagnosticsTest, NoURLAndEndWithSuccess) {
  ExpectSuccessfulStart(/*url=*/"");
  TriggerPingGatewayStartSuccess(kIPv4GatewayAddress);
  TriggerPingGatewayEndSuccess();
  ExpectPingDNSServersStartSuccess();
  TriggerPingDNSServersEndSuccess();
  VerifyStopped();
}

}  // namespace shill
