// Copyright 2021 The Chromium OS Authors. All rights reserved.
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
      : AresClient(base::TimeDelta::FromSeconds(1), 1, 1),
        provider_(provider) {}
  ~FakeAresClient() = default;

  bool Resolve(const unsigned char* msg,
               size_t len,
               const AresClient::QueryCallback& callback,
               void* ctx) override {
    return provider_->ConsumeBool();
  }

 private:
  FuzzedDataProvider* provider_;
};

class FakeCurlClient : public DoHCurlClientInterface {
 public:
  explicit FakeCurlClient(FuzzedDataProvider* provider) : provider_(provider) {}
  ~FakeCurlClient() = default;

  bool Resolve(const char*,
               int,
               const DoHCurlClient::QueryCallback&,
               void*) override {
    return provider_->ConsumeBool();
  }
  void SetNameServers(const std::vector<std::string>&) override {}
  void SetDoHProviders(const std::vector<std::string>&) override {}

 private:
  FuzzedDataProvider* provider_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  auto ares_client = std::make_unique<FakeAresClient>(&provider);
  auto curl_client = std::make_unique<FakeCurlClient>(&provider);
  Resolver resolver(std::move(ares_client), std::move(curl_client));

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

    auto msg = provider.ConsumeRandomLengthString(
        std::numeric_limits<uint16_t>::max());
    resolver.ConstructServFailResponse(msg.c_str(), msg.size());
  }

  return 0;
}

}  // namespace
}  // namespace dns_proxy
