// Copyright 2021 The ChromiumOS Authors
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
  DoHCurlClient curl_client(base::Seconds(1));

  while (provider.remaining_bytes() > 0) {
    auto msg =
        provider.ConsumeBytes<char>(std::numeric_limits<unsigned int>::max());
    curl_client.Resolve(
        base::span<const char>(msg.data(), msg.size()),
        base::BindRepeating([](const DoHCurlClientInterface::CurlResult&,
                               const base::span<unsigned char>&) {}),
        {"8.8.8.8"}, "https://dns.google/dns-query");
    base::RunLoop().RunUntilIdle();
  }
  return 0;
}

}  // namespace
}  // namespace dns_proxy
