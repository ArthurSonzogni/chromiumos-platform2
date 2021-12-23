// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <metrics/metrics_library_mock.h>
#include <trunks/fuzzed_command_transceiver.h>

#include "u2fd/allowlisting_util.h"
#include "u2fd/fuzzers/fuzzed_allowlisting_util_factory.h"
#include "u2fd/fuzzers/fuzzed_user_state.h"
#include "u2fd/u2f_msg_handler.h"

namespace {

constexpr size_t kMaxTpmMessageLength = 512;

// Provide max iterations for a single fuzz run, otherwise it might timeout.
constexpr int kMaxIterations = 100;

}  // namespace

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider data_provider(data, size);

  u2f::FuzzedAllowlistingUtilFactory allowlisting_util_factory(&data_provider);
  auto allowlisting_util = allowlisting_util_factory.CreateAllowlistingUtil();
  std::function<void()> request_presence = []() {
    // do nothing
  };
  auto user_state = std::make_unique<u2f::FuzzedUserState>(&data_provider);
  u2f::TpmVendorCommandProxy tpm_proxy(
      std::make_unique<trunks::FuzzedCommandTransceiver>(&data_provider,
                                                         kMaxTpmMessageLength));
  testing::NiceMock<MetricsLibraryMock> mock_metrics;
  bool legacy_kh_fallback = data_provider.ConsumeBool();
  bool allow_g2f_attestation = data_provider.ConsumeBool();

  auto u2f_msg_handler = std::make_unique<u2f::U2fMessageHandler>(
      std::move(allowlisting_util), request_presence, user_state.get(),
      &tpm_proxy, &mock_metrics, legacy_kh_fallback, allow_g2f_attestation);

  int rounds = 0;
  while (data_provider.remaining_bytes() > 0 && rounds < kMaxIterations) {
    u2f_msg_handler->ProcessMsg(data_provider.ConsumeRandomLengthString());
    user_state->NextState();
    rounds++;
  }

  return 0;
}
