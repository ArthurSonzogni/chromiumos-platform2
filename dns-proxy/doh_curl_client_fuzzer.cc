// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "dns-proxy/doh_curl_client.h"

#include <base/task/single_thread_task_executor.h>
#include <brillo/message_loops/base_message_loop.h>

namespace dns_proxy {
namespace {

class Environment {
 public:
  Environment() {
    // logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
  base::AtExitManager at_exit;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  brillo::BaseMessageLoop loop(task_executor.task_runner());
  loop.SetAsCurrent();

  FuzzedDataProvider provider(data, size);
  DoHCurlClient curl_client(base::TimeDelta::FromSeconds(1), 1);

  while (provider.remaining_bytes() > 0) {
    size_t n = provider.ConsumeIntegralInRange<size_t>(0, 99);
    std::vector<std::string> s;
    s.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      s.emplace_back(provider.ConsumeRandomLengthString(
          std::numeric_limits<unsigned int>::max()));
    }
    curl_client.SetNameServers(s);
    curl_client.SetDoHProviders(s);

    s.clear();
    s.emplace_back("8.8.8.8");
    curl_client.SetNameServers(s);
    s.clear();
    s.emplace_back("https://dns.google/dns-query");
    curl_client.SetDoHProviders(s);
    auto msg =
        provider.ConsumeBytes<char>(std::numeric_limits<unsigned int>::max());
    curl_client.Resolve(
        msg.data(), msg.size(),
        base::BindRepeating([](void*, const DoHCurlClientInterface::CurlResult&,
                               uint8_t*, size_t) {}),
        nullptr);
    base::RunLoop().RunUntilIdle();
  }
  return 0;
}

}  // namespace
}  // namespace dns_proxy
