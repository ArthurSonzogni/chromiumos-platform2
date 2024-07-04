// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <chromeos/net-base/mock_socket.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dns-proxy/ares_client.h"
#include "dns-proxy/doh_curl_client.h"

using testing::_;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Return;
using testing::UnorderedElementsAreArray;

namespace dns_proxy {
namespace {
const std::vector<std::string> kTestNameServers{"8.8.8.8", "8.8.4.4"};
const std::vector<std::string> kTestDoHProviders{
    "https://dns.google/dns-query", "https://dns2.google/dns-query"};
constexpr base::TimeDelta kTimeout = base::Seconds(3);

// Example of a valid DNS TCP fragment:
// - first 2 bytes are a TCP header containing the size of the DNS query
//   (0x0102 == 258).
// - next bytes are the DNS query with the length equals to the TCP
//   header.
constexpr uint8_t kDNSTCPFragment[] = {
    0x01, 0x02, 0xbe, 0x64, 0x4b, 0xdd, 0x74, 0xd6, 0x7d, 0x4c, 0x45, 0xe4,
    0x25, 0x25, 0x48, 0x53, 0x5a, 0x42, 0xc2, 0x32, 0xd7, 0xf5, 0x63, 0xcf,
    0x60, 0xd5, 0x51, 0x78, 0x18, 0x82, 0x39, 0xed, 0x81, 0x92, 0xaf, 0x4b,
    0x7c, 0xc2, 0x5a, 0x61, 0xd6, 0xf0, 0x0b, 0x5a, 0xbe, 0xb8, 0xc5, 0x80,
    0xeb, 0x36, 0x7f, 0xe4, 0x75, 0xa2, 0xdc, 0xc1, 0x34, 0x65, 0xb3, 0x52,
    0x34, 0x34, 0xe7, 0x60, 0x6b, 0x95, 0xeb, 0xa2, 0x69, 0x29, 0xd5, 0xd2,
    0x5d, 0x81, 0xa9, 0x42, 0x65, 0x40, 0xee, 0xd8, 0x78, 0xf5, 0xdc, 0xd4,
    0xa9, 0x62, 0xe9, 0x27, 0x1d, 0xb4, 0x22, 0xad, 0x59, 0xd6, 0x75, 0xb7,
    0x9a, 0x4c, 0x6e, 0x82, 0x44, 0x1c, 0x2e, 0xbc, 0xd8, 0x6c, 0xa5, 0x5b,
    0xa4, 0xa2, 0x9e, 0x41, 0x8e, 0x95, 0x4d, 0x75, 0x07, 0xef, 0x99, 0x10,
    0x4d, 0x64, 0x77, 0x0c, 0x1d, 0x84, 0x8d, 0xad, 0x39, 0xef, 0x86, 0x15,
    0x44, 0x3f, 0xf8, 0x7a, 0x7e, 0xc8, 0xc6, 0x96, 0x5c, 0x5c, 0x29, 0xc7,
    0xab, 0xfd, 0xff, 0x25, 0xb3, 0x4a, 0xec, 0x0d, 0x5d, 0x3a, 0x97, 0x1b,
    0x98, 0x5f, 0x9d, 0x4b, 0x99, 0x11, 0x6a, 0x21, 0x11, 0x11, 0xb7, 0x69,
    0xd2, 0x03, 0x6c, 0x22, 0x59, 0x11, 0xf1, 0x4e, 0xa5, 0xdd, 0x60, 0x24,
    0xa6, 0xf2, 0x55, 0xf1, 0xa7, 0x58, 0x16, 0x21, 0xac, 0xc5, 0x3f, 0xb9,
    0x77, 0xf7, 0x20, 0x08, 0xa1, 0x99, 0x3f, 0x96, 0x76, 0xae, 0x63, 0xb6,
    0xce, 0xac, 0x36, 0xda, 0x23, 0xa8, 0x13, 0xd3, 0x4e, 0x25, 0xa5, 0x85,
    0xd1, 0x28, 0x77, 0xdc, 0xd1, 0xb9, 0x09, 0x55, 0x78, 0x81, 0x61, 0x9b,
    0x67, 0x64, 0xe8, 0xb6, 0x6f, 0xfc, 0x0c, 0xd6, 0xf3, 0x33, 0xcf, 0xea,
    0x9d, 0x05, 0x62, 0x14, 0x21, 0xaf, 0xf7, 0xfd, 0x92, 0xd6, 0xac, 0x06,
    0x7d, 0x2d, 0xe2, 0x9b, 0x19, 0xaa, 0xfc, 0x79};

class MockDoHCurlClient : public DoHCurlClient {
 public:
  MockDoHCurlClient() : DoHCurlClient(kTimeout) {}
  ~MockDoHCurlClient() = default;

  MOCK_METHOD5(Resolve,
               bool(const char* msg,
                    int len,
                    const QueryCallback& callback,
                    const std::vector<std::string>&,
                    const std::string&));
};

class MockAresClient : public AresClient {
 public:
  MockAresClient() : AresClient(kTimeout) {}
  ~MockAresClient() = default;

  MOCK_METHOD5(Resolve,
               bool(const unsigned char* msg,
                    size_t len,
                    const QueryCallback& callback,
                    const std::string& name_server,
                    int type));
};

}  // namespace

class ResolverTest : public testing::Test {
 public:
  void SetNameServers(const std::vector<std::string>& name_servers,
                      bool validate = false) {
    resolver_->SetNameServers(name_servers);
    if (!validate) {
      return;
    }
    // Validate name servers.
    for (const auto& name_server : name_servers) {
      auto probe_state =
          std::make_unique<Resolver::ProbeState>(name_server, /*doh=*/false);
      resolver_->HandleDo53ProbeResult(probe_state->weak_factory.GetWeakPtr(),
                                       {}, ARES_SUCCESS, nullptr, 0);
    }
  }

  void SetDoHProviders(const std::vector<std::string>& doh_providers,
                       bool validate = false,
                       bool always_on_doh = false) {
    resolver_->SetDoHProviders(doh_providers, always_on_doh);
    if (!validate) {
      return;
    }
    // Validate DoH providers.
    for (const auto& doh_provider : doh_providers) {
      DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */,
                                    0 /* timeout */);
      auto probe_state =
          std::make_unique<Resolver::ProbeState>(doh_provider, /*doh=*/true);
      resolver_->HandleDoHProbeResult(probe_state->weak_factory.GetWeakPtr(),
                                      {}, res, nullptr, 0);
    }
  }

  void ValidateNameServer(const std::string& name_server) {
    auto probe_state =
        std::make_unique<Resolver::ProbeState>(name_server, /*doh=*/false);
    resolver_->HandleDo53ProbeResult(probe_state->weak_factory.GetWeakPtr(), {},
                                     ARES_SUCCESS, nullptr, 0);
  }

  void ValidateDoHProvider(const std::string& doh_provider) {
    DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */,
                                  0 /* timeout */);
    auto probe_state =
        std::make_unique<Resolver::ProbeState>(doh_provider, /*doh=*/true);
    resolver_->HandleDoHProbeResult(probe_state->weak_factory.GetWeakPtr(), {},
                                    res, nullptr, 0);
  }

  void InvalidateNameServer(const std::string& name_server) {
    // Resolve returns failure.
    auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, 0);
    auto probe_state = std::make_unique<Resolver::ProbeState>(
        name_server, /*doh=*/false, /*validated=*/true);
    resolver_->HandleAresResult(sock_fd->weak_factory.GetWeakPtr(),
                                probe_state->weak_factory.GetWeakPtr(),
                                ARES_ETIMEOUT, nullptr, 0);
  }

  void InvalidateDoHProvider(const std::string& doh_provider) {
    // Resolve returns failure.
    auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, 0);
    auto probe_state = std::make_unique<Resolver::ProbeState>(
        doh_provider, /*doh=*/true, /*validated=*/true);
    resolver_->HandleCurlResult(
        sock_fd->weak_factory.GetWeakPtr(),
        probe_state->weak_factory.GetWeakPtr(),
        DoHCurlClient::CurlResult(CURLE_OUT_OF_MEMORY, /*http_code=*/0,
                                  /*retry_delay_ms=*/0),
        nullptr, 0);
  }

 protected:
  void SetUp() override {
    auto scoped_ares_client = std::make_unique<MockAresClient>();
    auto scoped_curl_client = std::make_unique<MockDoHCurlClient>();
    auto socket_factory = std::make_unique<net_base::MockSocketFactory>();
    ares_client_ = scoped_ares_client.get();
    curl_client_ = scoped_curl_client.get();
    socket_factory_ = socket_factory.get();

    resolver_ = std::make_unique<Resolver>(std::move(scoped_ares_client),
                                           std::move(scoped_curl_client),
                                           std::move(socket_factory));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  MockAresClient* ares_client_;
  MockDoHCurlClient* curl_client_;
  net_base::MockSocketFactory* socket_factory_;
  std::unique_ptr<Resolver> resolver_;
};

TEST_F(ResolverTest, ListenTCP) {
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = 13568;
  addr.sin6_addr = in6addr_any;

  EXPECT_CALL(*socket_factory_,
              Create(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, _))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Bind(_, sizeof(struct sockaddr_in6)))
            .WillOnce(Return(true));
        EXPECT_CALL(*socket, Listen).WillOnce(Return(true));
        return socket;
      });

  EXPECT_TRUE(resolver_->ListenTCP(reinterpret_cast<struct sockaddr*>(&addr)));
}

TEST_F(ResolverTest, ListenUDP) {
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = 13568;
  addr.sin6_addr = in6addr_any;

  EXPECT_CALL(*socket_factory_, Create(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, _))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Bind(_, sizeof(struct sockaddr_in6)))
            .WillOnce(Return(true));
        return socket;
      });

  EXPECT_TRUE(resolver_->ListenUDP(reinterpret_cast<struct sockaddr*>(&addr)));
}

TEST_F(ResolverTest, SetNameServers) {
  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _))
        .WillOnce(Return(true));
  }
  SetNameServers(kTestNameServers, /*validate=*/true);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
}

TEST_F(ResolverTest, SetDoHProviders) {
  for (const auto& doh_provider : kTestDoHProviders) {
    EXPECT_CALL(*curl_client_,
                Resolve(_, _, _, UnorderedElementsAreArray(kTestNameServers),
                        doh_provider))
        .WillOnce(Return(true));
  }
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true, /*always_on_doh=*/true);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
}

TEST_F(ResolverTest, Resolve_DNSDoHServersNotValidated) {
  SetNameServers(kTestNameServers);
  SetDoHProviders(kTestDoHProviders);

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersPartiallyValidated) {
  SetNameServers(kTestNameServers);
  SetDoHProviders(kTestDoHProviders);

  const auto& validated_doh_provider = kTestDoHProviders.front();
  ValidateDoHProvider(validated_doh_provider);

  // Expect resolving to be only be done using the validated provider.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, validated_doh_provider))
      .WillOnce(Return(true));

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersValidated) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  // Expect resolving to be be done using all validated providers.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .Times(kTestDoHProviders.size())
      .WillRepeatedly(Return(true));

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSServers) {
  SetNameServers(kTestNameServers, /*validate=*/true);

  // Expect resolving to be be done using all validated name servers.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackNotValidated) {
  SetNameServers(kTestNameServers);
  SetDoHProviders(kTestDoHProviders);

  // Expect resolving to be be done using all name servers when nothing is
  // validated.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackPartiallyValidated) {
  SetNameServers(kTestNameServers);
  SetDoHProviders(kTestDoHProviders);

  const auto& validated_name_server = kTestNameServers.front();
  ValidateNameServer(validated_name_server);

  // Expect resolving to be only be done using the validated name server.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, validated_name_server, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackValidated) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  // Expect fallback resolving to be done using validated name servers.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr(), true);
  EXPECT_GT(sock_fd->num_active_queries, 0);
}

TEST_F(ResolverTest, CurlResult_CURLFail) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect query to be done with Do53.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  // All curl results failed with curl error.
  DoHCurlClient::CurlResult res(CURLE_COULDNT_CONNECT, 0 /* http_code */,
                                0 /* timeout */);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_HTTPError) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect query to be done with Do53.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  // All curl results failed with a HTTP error.
  DoHCurlClient::CurlResult res(CURLE_OK, 403 /* http_code */, 0 /*timeout*/);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_SuccessNoRetry) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect no more queries.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */, 0 /*timeout*/);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_CurlErrorNoRetry) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true, /*always_on_doh=*/true);

  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect no more queries.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  DoHCurlClient::CurlResult res(CURLE_OUT_OF_MEMORY, 0 /* http_code */,
                                0 /* timeout */);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_HTTPErrorNoRetry) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true, /*always_on_doh=*/true);

  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect no more queries.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  DoHCurlClient::CurlResult res(CURLE_OK, 403 /* http_code */, 0 /* timeout*/);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_FailTooManyRetries) {
  SetNameServers(kTestNameServers, /*validate=*/true);
  SetDoHProviders(kTestDoHProviders, /*validate=*/true);

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect no more queries.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  sock_fd->num_retries = INT_MAX;
  DoHCurlClient::CurlResult res(CURLE_OK, 429 /* http_code */, 0 /*timeout*/);
  for (int i = 0; i < kTestDoHProviders.size(); i++) {
    resolver_->HandleCurlResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                res, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, HandleAresResult_Success) {
  SetNameServers(kTestNameServers, /*validate=*/true);

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .WillRepeatedly(Return(true));
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, 0);
  resolver_->Resolve(sock_fd->weak_factory.GetWeakPtr());
  EXPECT_GT(sock_fd->num_active_queries, 0);

  // Expect no more queries.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  sock_fd->num_retries = INT_MAX;
  for (int i = 0; i < kTestNameServers.size(); i++) {
    resolver_->HandleAresResult(sock_fd->weak_factory.GetWeakPtr(), nullptr,
                                ARES_SUCCESS, nullptr, 0);
  }
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, ConstructServFailResponse_ValidQuery) {
  const char kDnsQuery[] = {'J',    'G',    '\x01', ' ',    '\x00', '\x01',
                            '\x00', '\x00', '\x00', '\x00', '\x00', '\x01',
                            '\x06', 'g',    'o',    'o',    'g',    'l',
                            'e',    '\x03', 'c',    'o',    'm',    '\x00',
                            '\x00', '\x01', '\x00', '\x01'};
  const char kServFailResponse[] = {
      'J',    'G',    '\x80', '\x02', '\x00', '\x01', '\x00',
      '\x00', '\x00', '\x00', '\x00', '\x00', '\x06', 'g',
      'o',    'o',    'g',    'l',    'e',    '\x03', 'c',
      'o',    'm',    '\x00', '\x00', '\x01', '\x00', '\x01'};
  patchpanel::DnsResponse response =
      resolver_->ConstructServFailResponse(kDnsQuery, sizeof(kDnsQuery));
  std::vector<char> response_data(
      response.io_buffer()->data(),
      response.io_buffer()->data() + response.io_buffer_size());
  EXPECT_THAT(response_data, ElementsAreArray(kServFailResponse));
}

TEST_F(ResolverTest, ConstructServFailResponse_BadLength) {
  const char kDnsQuery[] = {'J',    'G',    '\x01', ' ',    '\x00', '\x01',
                            '\x00', '\x00', '\x00', '\x00', '\x00', '\x01',
                            '\x06', 'g',    'o',    'o',    'g',    'l',
                            'e',    '\x03', 'c',    'o',    'm',    '\x00',
                            '\x00', '\x01', '\x00', '\x01'};
  const char kServFailResponse[] = {'\x00', '\x00', '\x80', '\x02',
                                    '\x00', '\x00', '\x00', '\x00',
                                    '\x00', '\x00', '\x00', '\x00'};
  patchpanel::DnsResponse response =
      resolver_->ConstructServFailResponse(kDnsQuery, -1);
  std::vector<char> response_data(
      response.io_buffer()->data(),
      response.io_buffer()->data() + response.io_buffer_size());
  EXPECT_THAT(response_data, ElementsAreArray(kServFailResponse));
}

TEST_F(ResolverTest, ConstructServFailResponse_BadQuery) {
  const char kDnsQuery[] = {'g', 'o',    'o', 'g', 'l',
                            'e', '\x03', 'c', 'o', 'm'};
  const char kServFailResponse[] = {'\x00', '\x00', '\x80', '\x02',
                                    '\x00', '\x00', '\x00', '\x00',
                                    '\x00', '\x00', '\x00', '\x00'};
  patchpanel::DnsResponse response =
      resolver_->ConstructServFailResponse(kDnsQuery, sizeof(kDnsQuery));
  std::vector<char> response_data(
      response.io_buffer()->data(),
      response.io_buffer()->data() + response.io_buffer_size());
  EXPECT_THAT(response_data, ElementsAreArray(kServFailResponse));
}

TEST_F(ResolverTest, Probe_Started) {
  resolver_->SetProbingEnabled(true);

  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _))
        .WillOnce(Return(true));
  }
  for (const auto& doh_provider : kTestDoHProviders) {
    EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, doh_provider))
        .WillOnce(Return(true));
  }

  SetNameServers(kTestNameServers);
  SetDoHProviders(kTestDoHProviders);
}

TEST_F(ResolverTest, Probe_SetNameServers) {
  resolver_->SetProbingEnabled(true);

  auto name_servers = kTestNameServers;
  for (const auto& name_server : name_servers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _))
        .WillOnce(Return(true));
  }

  const auto& new_name_server = "9.9.9.9";
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, new_name_server, _)).Times(0);

  SetNameServers(name_servers);

  name_servers.push_back(new_name_server);

  // Check that only the newly added name servers are probed.
  for (const auto& name_server : name_servers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _)).Times(0);
  }
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, new_name_server, _))
      .WillOnce(Return(true));

  SetNameServers(name_servers);
}

TEST_F(ResolverTest, Probe_SetDoHProviders) {
  resolver_->SetProbingEnabled(true);

  auto doh_providers = kTestDoHProviders;
  for (const auto& doh_provider : doh_providers) {
    EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, doh_provider))
        .WillOnce(Return(true));
  }

  const auto& new_doh_provider = "https://dns3.google/dns-query";
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, new_doh_provider)).Times(0);

  SetNameServers(kTestNameServers);
  SetDoHProviders(doh_providers);

  doh_providers.push_back(new_doh_provider);

  // Check that only the newly added DoH providers and name servers are probed.
  for (const auto& doh_provider : doh_providers) {
    EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, doh_provider)).Times(0);
  }
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, new_doh_provider))
      .WillOnce(Return(true));

  SetDoHProviders(doh_providers);
}

TEST_F(ResolverTest, Probe_InvalidateNameServer) {
  auto name_servers = kTestNameServers;
  SetNameServers(name_servers, /*validate=*/true);

  // Invalidate a name server.
  auto invalidated_name_server = name_servers.back();
  name_servers.pop_back();
  InvalidateNameServer(invalidated_name_server);

  // Query should be done using all name servers except the invalidated one.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, invalidated_name_server, _))
      .Times(0);
  for (const auto& name_server : name_servers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _))
        .WillOnce(Return(true));
  }

  auto fd_check = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, 0);
  resolver_->Resolve(fd_check->weak_factory.GetWeakPtr());
  EXPECT_GT(fd_check->num_active_queries, 0);
}

TEST_F(ResolverTest, Probe_InvalidateDoHProvider) {
  auto doh_providers = kTestDoHProviders;
  SetNameServers(kTestNameServers);
  SetDoHProviders(doh_providers, /*validate=*/true);

  // Invalidate a DoH provider.
  auto invalidated_doh_provider = doh_providers.back();
  doh_providers.pop_back();
  InvalidateDoHProvider(invalidated_doh_provider);

  // Query should be done using all DoH providers except the invalidated one.
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, invalidated_doh_provider))
      .Times(0);
  for (const auto& doh_provider : doh_providers) {
    EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, doh_provider))
        .WillOnce(Return(true));
  }

  auto fd_check = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, 0);
  resolver_->Resolve(fd_check->weak_factory.GetWeakPtr());
  EXPECT_GT(fd_check->num_active_queries, 0);
}

TEST_F(ResolverTest, Probe_Do53ProbeRestarted) {
  auto name_servers = kTestNameServers;
  SetNameServers(name_servers, /*validate=*/true);
  auto invalidated_name_server = name_servers.back();
  name_servers.pop_back();

  // Expect probe to be restarted only for the invalidated name server.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, invalidated_name_server, _))
      .WillOnce(Return(true));
  for (const auto& name_server : name_servers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _)).Times(0);
  }
  resolver_->SetProbingEnabled(true);

  // Invalidate a name server.
  InvalidateNameServer(invalidated_name_server);
}

TEST_F(ResolverTest, Probe_DoHProbeRestarted) {
  auto doh_providers = kTestDoHProviders;
  SetNameServers(kTestNameServers);
  SetDoHProviders(doh_providers, /*validate=*/true);
  auto invalidated_doh_provider = doh_providers.back();
  doh_providers.pop_back();

  // Expect probe to be restarted only for the invalidated DoH provider.
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, invalidated_doh_provider))
      .WillOnce(Return(true));
  for (const auto& doh_provider : doh_providers) {
    EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, doh_provider)).Times(0);
  }
  resolver_->SetProbingEnabled(true);

  // Invalidate a DoH provider.
  InvalidateDoHProvider(invalidated_doh_provider);
}

TEST_F(ResolverTest, Resolve_HandleUDPQuery) {
  SetNameServers(kTestNameServers);
  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, SOCK_DGRAM))
        .WillOnce(Return(true));
  }
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_DGRAM, /*fd=*/0);
  resolver_->HandleDNSQuery(std::move(sock_fd));
}

TEST_F(ResolverTest, Resolve_HandleTCPQuery) {
  SetNameServers(kTestNameServers);
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, /*fd=*/0);
  memcpy(sock_fd->msg, kDNSTCPFragment, sizeof(kDNSTCPFragment));
  sock_fd->len = sizeof(kDNSTCPFragment);

  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, SOCK_STREAM))
        .WillOnce(Return(true));
  }
  resolver_->HandleDNSQuery(std::move(sock_fd));
}

TEST_F(ResolverTest, Resolve_HandleChunkedTCPQuery) {
  SetNameServers(kTestNameServers);

  int partial_len = 15;
  ASSERT_LT(partial_len, sizeof(kDNSTCPFragment));

  // Send partial TCP data.
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, /*fd=*/0);
  sock_fd->buf.resize(kTCPBufferPaddingLength + sizeof(kDNSTCPFragment));
  memcpy(sock_fd->msg, kDNSTCPFragment, partial_len);
  sock_fd->len = partial_len;

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  resolver_->HandleDNSQuery(std::move(sock_fd));

  // Send remaining TCP data.
  sock_fd = resolver_->PopPendingSocketFd(/*fd=*/0);
  memcpy(sock_fd->msg + partial_len, kDNSTCPFragment + partial_len,
         sizeof(kDNSTCPFragment) - partial_len);
  sock_fd->len += sizeof(kDNSTCPFragment) - partial_len;

  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, SOCK_STREAM))
        .WillOnce(Return(true));
  }
  resolver_->HandleDNSQuery(std::move(sock_fd));
}

TEST_F(ResolverTest, Resolve_HandleMultipleTCPQueries) {
  SetNameServers(kTestNameServers);

  // Send 2 TCP DNS queries.
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, /*fd=*/0);
  sock_fd->buf.resize(kTCPBufferPaddingLength + 2 * sizeof(kDNSTCPFragment));
  memcpy(sock_fd->msg, kDNSTCPFragment, sizeof(kDNSTCPFragment));
  memcpy(sock_fd->msg + sizeof(kDNSTCPFragment), kDNSTCPFragment,
         sizeof(kDNSTCPFragment));
  sock_fd->len = 2 * sizeof(kDNSTCPFragment);

  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, SOCK_STREAM))
        .Times(2)
        .WillRepeatedly(Return(true));
  }
  resolver_->HandleDNSQuery(std::move(sock_fd));
}

TEST_F(ResolverTest, Resolve_ChunkedTCPQueryNotResolved) {
  SetNameServers(kTestNameServers);

  // Expect no resolving.
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);

  // Receive 1-byte at a time.
  auto sock_fd = std::make_unique<Resolver::SocketFd>(SOCK_STREAM, /*fd=*/0);
  sock_fd->buf.resize(kTCPBufferPaddingLength + sizeof(kDNSTCPFragment));
  memcpy(sock_fd->msg, kDNSTCPFragment, 1);
  sock_fd->len += 1;

  // Receive all data except for the last byte.
  while (sock_fd->len < sizeof(kDNSTCPFragment)) {
    resolver_->HandleDNSQuery(std::move(sock_fd));
    sock_fd = resolver_->PopPendingSocketFd(/*fd=*/0);
    memcpy(sock_fd->msg + sock_fd->len, kDNSTCPFragment + sock_fd->len, 1);
    sock_fd->len += 1;
  }
}

TEST_F(ResolverTest, SocketFd_Resize) {
  for (const int sock_type : {SOCK_STREAM, SOCK_DGRAM}) {
    auto sock_fd = std::make_unique<Resolver::SocketFd>(sock_type, /*fd=*/0);

    // Expects buffer size to not grow when not full.
    int cur_size = sock_fd->try_resize();
    EXPECT_EQ(sock_fd->buf.size(), cur_size);

    // Expects buffer size to grow.
    while (cur_size < kMaxDNSBufSize) {
      sock_fd->len = sock_fd->buf.size();
      if (sock_fd->type == SOCK_STREAM) {
        sock_fd->len -= kTCPBufferPaddingLength;
      }
      EXPECT_GT(sock_fd->try_resize(), cur_size);
      cur_size = sock_fd->buf.size();
    }
    EXPECT_EQ(sock_fd->buf.size(), kMaxDNSBufSize);

    // Expects buffer size to no longer grow after maximum size.
    EXPECT_EQ(sock_fd->try_resize(), kMaxDNSBufSize);
  }
}

TEST_F(ResolverTest, IsNXDOMAIN_NXDOMAIN) {
  const char kDnsResponse[] = {'\x00', '\x01', '\x81', '\x83', '\x00', '\x01',
                               '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
                               '\x06', 'g',    'o',    'o',    'g',    'l',
                               'e',    '\x03', 'c',    'o',    'm',    '\x00',
                               '\x00', '\x01', '\x00', '\x01'};
  EXPECT_TRUE(resolver_->IsNXDOMAIN(
      reinterpret_cast<const unsigned char*>(kDnsResponse),
      sizeof(kDnsResponse)));
}

TEST_F(ResolverTest, IsNXDOMAIN_NotNXDOMAIN) {
  const char kDnsResponse[] = {'\x00', '\x01', '\x81', '\x81', '\x00', '\x01',
                               '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
                               '\x06', 'g',    'o',    'o',    'g',    'l',
                               'e',    '\x03', 'c',    'o',    'm',    '\x00',
                               '\x00', '\x01', '\x00', '\x01'};
  EXPECT_FALSE(resolver_->IsNXDOMAIN(
      reinterpret_cast<const unsigned char*>(kDnsResponse),
      sizeof(kDnsResponse)));
}

TEST_F(ResolverTest, IsNXDOMAIN_BadResponse) {
  const char kDnsResponse[] = {'\x00', '\x01', '\x02', '\x03', '\x04', '\x05'};
  EXPECT_FALSE(resolver_->IsNXDOMAIN(
      reinterpret_cast<const unsigned char*>(kDnsResponse),
      sizeof(kDnsResponse)));
}

TEST_F(ResolverTest, BypassDoH_LiteralMatch) {
  std::vector<std::string> doh_excluded_domains = {"google.com"};
  resolver_->SetDomainDoHConfigs(/*doh_included_domains=*/{},
                                 doh_excluded_domains);
  EXPECT_TRUE(resolver_->BypassDoH("google.com"));
  EXPECT_FALSE(resolver_->BypassDoH("a.google.com"));
}

TEST_F(ResolverTest, BypassDoH_SuffixMatch) {
  std::vector<std::string> doh_excluded_domains = {"*.google.com"};
  resolver_->SetDomainDoHConfigs(/*doh_included_domains=*/{},
                                 doh_excluded_domains);
  EXPECT_FALSE(resolver_->BypassDoH("google.com"));
  EXPECT_TRUE(resolver_->BypassDoH("a.google.com"));
}

TEST_F(ResolverTest, BypassDoH_PreferMoreSpecificMatch) {
  std::vector<std::string> doh_excluded_domains = {"*.exclude",
                                                   "exclude.include"};
  std::vector<std::string> doh_included_domains = {"*.include",
                                                   "include.exclude"};
  resolver_->SetDomainDoHConfigs(doh_included_domains, doh_excluded_domains);
  EXPECT_TRUE(resolver_->BypassDoH("test.exclude"));
  EXPECT_FALSE(resolver_->BypassDoH("test.include"));
  EXPECT_TRUE(resolver_->BypassDoH("exclude.include"));
  EXPECT_FALSE(resolver_->BypassDoH("include.exclude"));
}

TEST_F(ResolverTest, BypassDoH_IncludeOverExclude) {
  std::vector<std::string> doh_excluded_domains = {"*.google.com",
                                                   "google.com"};
  std::vector<std::string> doh_included_domains = {"*.google.com",
                                                   "google.com"};
  resolver_->SetDomainDoHConfigs(doh_included_domains, doh_excluded_domains);
  EXPECT_FALSE(resolver_->BypassDoH("google.com"));
  EXPECT_FALSE(resolver_->BypassDoH("a.google.com"));
}

TEST_F(ResolverTest, BypassDoH_DefaultsToInclude) {
  std::vector<std::string> doh_excluded_domains = {"unused"};
  resolver_->SetDomainDoHConfigs(/*doh_included_domains=*/{},
                                 doh_excluded_domains);
  EXPECT_FALSE(resolver_->BypassDoH("google.com"));
}

TEST_F(ResolverTest, BypassDoH_ExcludeNotIncludedDomains) {
  std::vector<std::string> doh_included_domains = {"google.com"};
  resolver_->SetDomainDoHConfigs(doh_included_domains,
                                 /*doh_excluded_domains=*/{});
  EXPECT_TRUE(resolver_->BypassDoH("test.com"));
  EXPECT_FALSE(resolver_->BypassDoH("google.com"));
}
}  // namespace dns_proxy
