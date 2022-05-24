// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/time/time.h>
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
 protected:
  void SetUp() override {
    std::unique_ptr<MockAresClient> scoped_ares_client(new MockAresClient());
    std::unique_ptr<MockDoHCurlClient> scoped_curl_client(
        new MockDoHCurlClient());
    ares_client_ = scoped_ares_client.get();
    curl_client_ = scoped_curl_client.get();
    resolver_ = std::make_unique<Resolver>(std::move(scoped_ares_client),
                                           std::move(scoped_curl_client));
  }

  base::test::TaskEnvironment task_environment_;

  MockAresClient* ares_client_;
  MockDoHCurlClient* curl_client_;
  std::unique_ptr<Resolver> resolver_;
};

TEST_F(ResolverTest, SetNameServers) {
  for (const auto& name_server : kTestNameServers) {
    EXPECT_CALL(*ares_client_, Resolve(_, _, _, name_server, _))
        .WillOnce(Return(true));
  }

  resolver_->SetNameServers(kTestNameServers);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, SetDoHProviders) {
  for (const auto& doh_provider : kTestDoHProviders) {
    EXPECT_CALL(*curl_client_,
                Resolve(_, _, _, UnorderedElementsAreArray(kTestNameServers),
                        doh_provider))
        .WillOnce(Return(true));
  }

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders, /*always_on_doh=*/true);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersNotValidated) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersPartiallyValidated) {
  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  const auto& validated_doh_provider = kTestDoHProviders.front();
  DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */, 0 /* timeout */);
  auto probe_state = std::make_unique<Resolver::ProbeState>();
  resolver_->HandleDoHProbeResult(validated_doh_provider,
                                  probe_state->weak_factory.GetWeakPtr(), res,
                                  nullptr, 0);

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, validated_doh_provider))
      .WillOnce(Return(true));

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersValidated) {
  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  // Validate DoH providers and name servers.
  for (const auto& doh_provider : kTestDoHProviders) {
    DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */,
                                  0 /* timeout */);
    auto probe_state = std::make_unique<Resolver::ProbeState>();
    resolver_->HandleDoHProbeResult(
        doh_provider, probe_state->weak_factory.GetWeakPtr(), res, nullptr, 0);
  }
  for (const auto& name_server : kTestNameServers) {
    auto probe_state = std::make_unique<Resolver::ProbeState>();
    resolver_->HandleDo53ProbeResult(name_server,
                                     probe_state->weak_factory.GetWeakPtr(),
                                     ARES_SUCCESS, nullptr, 0);
  }

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _))
      .Times(kTestDoHProviders.size())
      .WillRepeatedly(Return(true));

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, Resolve_DNSServers) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0);
  resolver_->Resolve(&sock_fd);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackNotValidated) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0);
  resolver_->Resolve(&sock_fd, true);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackPartiallyValidated) {
  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  const auto& validated_name_server = kTestNameServers.front();
  auto probe_state = std::make_unique<Resolver::ProbeState>();
  resolver_->HandleDo53ProbeResult(validated_name_server,
                                   probe_state->weak_factory.GetWeakPtr(),
                                   ARES_SUCCESS, nullptr, 0);

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, validated_name_server, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  resolver_->Resolve(&sock_fd, true);
}

TEST_F(ResolverTest, Resolve_DNSDoHServersFallbackValidated) {
  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  // Validate DoH providers and name servers.
  for (const auto& doh_provider : kTestDoHProviders) {
    DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */,
                                  0 /* timeout */);
    auto probe_state = std::make_unique<Resolver::ProbeState>();
    resolver_->HandleDoHProbeResult(
        doh_provider, probe_state->weak_factory.GetWeakPtr(), res, nullptr, 0);
  }
  for (const auto& name_server : kTestNameServers) {
    auto probe_state = std::make_unique<Resolver::ProbeState>();
    resolver_->HandleDo53ProbeResult(name_server,
                                     probe_state->weak_factory.GetWeakPtr(),
                                     ARES_SUCCESS, nullptr, 0);
  }

  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  resolver_->Resolve(&sock_fd, true);
}

TEST_F(ResolverTest, CurlResult_CURLFail) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  DoHCurlClient::CurlResult res(CURLE_COULDNT_CONNECT, 0 /* http_code */,
                                0 /* timeout */);
  resolver_->HandleCurlResult(&sock_fd, res, nullptr, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_HTTPError) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _))
      .Times(kTestNameServers.size())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd sock_fd(SOCK_STREAM, 0, 1);
  DoHCurlClient::CurlResult res(CURLE_OK, 403 /* http_code */, 0 /* timeout */);
  resolver_->HandleCurlResult(&sock_fd, res, nullptr, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_SuccessNoRetry) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd* sock_fd = new Resolver::SocketFd(SOCK_STREAM, 0, 1);
  DoHCurlClient::CurlResult res(CURLE_OK, 200 /* http_code */, 0 /* timeout */);
  resolver_->HandleCurlResult(sock_fd, res, nullptr, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_FailNoRetry) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders, true /* always_on */);

  Resolver::SocketFd* sock_fd = new Resolver::SocketFd(SOCK_STREAM, 0, 1);
  DoHCurlClient::CurlResult res1(CURLE_OUT_OF_MEMORY, 200 /* http_code */,
                                 0 /* timeout */);
  resolver_->HandleCurlResult(sock_fd, res1, nullptr, 0);
  task_environment_.RunUntilIdle();

  // |sock_fd| should be freed by now.
  sock_fd = new Resolver::SocketFd(SOCK_STREAM, 0, 1);
  DoHCurlClient::CurlResult res2(CURLE_OK, 403 /* http_code */,
                                 0 /* timeout */);
  resolver_->HandleCurlResult(sock_fd, res2, nullptr, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, CurlResult_FailTooManyRetries) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);

  Resolver::SocketFd* sock_fd = new Resolver::SocketFd(SOCK_STREAM, 0, 1);
  sock_fd->num_retries = INT_MAX;
  DoHCurlClient::CurlResult res(CURLE_OK, 429 /* http_code */, 0 /* timeout */);
  resolver_->HandleCurlResult(sock_fd, res, nullptr, 0);
  task_environment_.RunUntilIdle();
}

TEST_F(ResolverTest, HandleAresResult_Success) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);

  Resolver::SocketFd* sock_fd = new Resolver::SocketFd(SOCK_DGRAM, 0, 1);
  resolver_->HandleAresResult(sock_fd, ARES_SUCCESS, nullptr, 0);
}

TEST_F(ResolverTest, HandleAresResult_Fail) {
  EXPECT_CALL(*ares_client_, Resolve(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*curl_client_, Resolve(_, _, _, _, _)).Times(0);

  resolver_->SetNameServers(kTestNameServers);

  Resolver::SocketFd* sock_fd = new Resolver::SocketFd(SOCK_DGRAM, 0, 1);
  resolver_->HandleAresResult(sock_fd, ARES_SUCCESS, nullptr, 0);
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

  resolver_->SetNameServers(kTestNameServers);
  resolver_->SetDoHProviders(kTestDoHProviders);
}
}  // namespace dns_proxy
