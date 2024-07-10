// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "dns-proxy/ares_client.h"
#include "dns-proxy/doh_curl_client.h"
#include "dns-proxy/resolver.h"

namespace dns_proxy {
namespace {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
  base::AtExitManager at_exit;
};

class FakeAresClient : public AresClient {
 public:
  explicit FakeAresClient(FuzzedDataProvider* provider)
      : AresClient(base::Seconds(1)), provider_(provider) {}
  ~FakeAresClient() = default;

  bool Resolve(const base::span<const unsigned char>& query,
               const AresClient::QueryCallback& callback,
               const std::string& name_server,
               int type) override {
    return provider_->ConsumeBool();
  }

 private:
  FuzzedDataProvider* provider_;
};

class FakeCurlClient : public DoHCurlClientInterface {
 public:
  explicit FakeCurlClient(FuzzedDataProvider* provider) : provider_(provider) {}
  ~FakeCurlClient() = default;

  bool Resolve(const base::span<const char>&,
               const DoHCurlClient::QueryCallback&,
               const std::vector<std::string>&,
               const std::string&) override {
    return provider_->ConsumeBool();
  }

 private:
  FuzzedDataProvider* provider_;
};

// Test class that overrides Resolver's receive function with stubs.
class TestResolver : public Resolver {
 public:
  TestResolver(std::unique_ptr<AresClient> ares_client,
               std::unique_ptr<DoHCurlClientInterface> curl_client,
               std::unique_ptr<net_base::SocketFactory> socket_factory)
      : Resolver(std::move(ares_client),
                 std::move(curl_client),
                 std::move(socket_factory)) {}

  TestResolver(const TestResolver&) = delete;
  TestResolver& operator=(const TestResolver&) = delete;
  ~TestResolver() override = default;

  ssize_t Receive(int fd,
                  char* buffer,
                  size_t buffer_size,
                  struct sockaddr* src_addr,
                  socklen_t* addrlen) override {
    buffer_size = std::min(payload.size(), buffer_size);
    if (buffer_size > 0) {
      memcpy(buffer, payload.data(), buffer_size);
      payload.erase(payload.begin(), payload.begin() + buffer_size);
    }
    if (addrlen == 0) {
      return buffer_size;
    }

    // Handle UDP sockets.
    *addrlen = std::min(static_cast<uint32_t>(src_sockaddr.size()), *addrlen);
    if (*addrlen > 0) {
      memcpy(src_addr, src_sockaddr.data(), *addrlen);
    }
    return buffer_size;
  }

  std::vector<uint8_t> src_sockaddr;
  std::vector<uint8_t> payload;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  auto ares_client = std::make_unique<FakeAresClient>(&provider);
  auto curl_client = std::make_unique<FakeCurlClient>(&provider);
  auto socket_factory = std::make_unique<net_base::SocketFactory>();
  TestResolver resolver(std::move(ares_client), std::move(curl_client),
                        std::move(socket_factory));

  while (provider.remaining_bytes() > 0) {
    size_t n = provider.ConsumeIntegralInRange<size_t>(0, 99);
    std::vector<std::string> s;
    s.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      s.emplace_back(provider.ConsumeRandomLengthString(
          std::numeric_limits<uint16_t>::max()));
    }
    resolver.SetNameServers(s);
    resolver.SetDoHProviders(s, provider.ConsumeBool());

    std::vector<std::string> doh_excluded_domains;
    std::vector<std::string> doh_included_domains;
    doh_excluded_domains.reserve(n);
    doh_included_domains.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      doh_excluded_domains.emplace_back(provider.ConsumeRandomLengthString(
          std::numeric_limits<uint16_t>::max()));
      doh_included_domains.emplace_back(provider.ConsumeRandomLengthString(
          std::numeric_limits<uint16_t>::max()));
    }

    resolver.SetDomainDoHConfigs(doh_excluded_domains, doh_included_domains);

    auto msg = provider.ConsumeRandomLengthString(
        std::numeric_limits<uint16_t>::max());
    resolver.ConstructServFailResponse(
        base::span<const char>(msg.c_str(), msg.size()));
    resolver.GetDNSQuestionName(base::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size()));
    resolver.BypassDoH(msg);

    int type = SOCK_STREAM;
    if (provider.ConsumeBool()) {
      type = SOCK_DGRAM;
      if (provider.ConsumeBool()) {
        resolver.src_sockaddr =
            provider.ConsumeBytes<uint8_t>(sizeof(struct sockaddr_in));
      } else {
        resolver.src_sockaddr =
            provider.ConsumeBytes<uint8_t>(sizeof(struct sockaddr_in6));
      }
    }
    resolver.payload =
        provider.ConsumeBytes<uint8_t>(provider.ConsumeIntegralInRange(
            0, 2 * static_cast<int>(kMaxDNSBufSize)));
    while (resolver.payload.size() > 0) {
      resolver.OnDNSQuery(/*fd=*/type == SOCK_STREAM ? 0 : 1, type);
    }
  }

  return 0;
}

}  // namespace
}  // namespace dns_proxy
