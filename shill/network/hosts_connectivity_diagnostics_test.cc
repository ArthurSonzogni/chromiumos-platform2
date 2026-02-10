// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <gtest/gtest.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

#include "shill/store/key_value_store.h"

namespace shill {
namespace {

using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;
using TestConnectivityResponse =
    hosts_connectivity_diagnostics::TestConnectivityResponse;

constexpr std::string_view kExampleDotCom = "example.com";
constexpr std::string_view kHttpsExampleDotCom = "https://example.com";
constexpr char kLoggingTag[] = "test_logging_tag";

class HostsConnectivityDiagnosticsTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(std::move(options));
    diagnostics_ =
        std::make_unique<HostsConnectivityDiagnostics>(mock_bus_, kLoggingTag);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  scoped_refptr<dbus::MockBus> mock_bus_;
  std::unique_ptr<HostsConnectivityDiagnostics> diagnostics_;
};

TEST_F(HostsConnectivityDiagnosticsTest, EmptyHostsListReturnsNoValidHostname) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::NO_VALID_HOSTNAME);
  EXPECT_EQ(response.connectivity_results(0).error_message(), kNoHostsProvided);
}

TEST_F(HostsConnectivityDiagnosticsTest, HostnameWithoutSchemeGetsHttpsPrefix) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(kExampleDotCom);
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Valid hostname passes through NormalizeHostnames to RunConnectivityTests
  // which still returns INTERNAL_ERROR (skeleton).
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

TEST_F(HostsConnectivityDiagnosticsTest, HttpsHostnameIsAccepted) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(kHttpsExampleDotCom);
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

TEST_F(HostsConnectivityDiagnosticsTest, InvalidHostnamesAreRejected) {
  auto verify_no_valid_hostname = [this](std::string_view hostname) {
    SCOPED_TRACE(hostname);
    base::test::TestFuture<const TestConnectivityResponse&> future;

    HostsConnectivityDiagnostics::RequestInfo request_info;
    request_info.raw_hostnames.emplace_back(hostname);
    request_info.callback = future.GetCallback();
    diagnostics_->TestHostsConnectivity(std::move(request_info));

    const auto& response = future.Get();
    ASSERT_EQ(response.connectivity_results_size(), 1);
    const auto& result = response.connectivity_results(0);
    EXPECT_EQ(result.result_code(), ResultCode::NO_VALID_HOSTNAME);
    EXPECT_EQ(result.hostname(), hostname);
    EXPECT_EQ(result.error_message(), kInvalidHostname);
  };

  // IPv4 addresses should be rejected.
  verify_no_valid_hostname("192.168.1.1");

  // IPv6 addresses should be rejected.
  verify_no_valid_hostname("https://[::1]");
  verify_no_valid_hostname("[2001:db8::1]");

  // localhost should be rejected.
  verify_no_valid_hostname("localhost");

  // URLs with paths should be rejected.
  verify_no_valid_hostname("https://example.com/path");

  // URLs with query parameters should be rejected.
  verify_no_valid_hostname("https://example.com?query=value");

  // URLs with userinfo should be rejected.
  verify_no_valid_hostname("https://user@example.com");
}

TEST_F(HostsConnectivityDiagnosticsTest, MixedValidAndInvalidHostnames) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"https://valid.com", "192.168.1.1",
                                "another-valid.com"};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  // 1 invalid (IP) + 1 skeleton INTERNAL_ERROR (for 2 valid hostnames).
  int invalid_count = 0;
  int internal_error_count = 0;
  for (int i = 0; i < response.connectivity_results_size(); i++) {
    if (response.connectivity_results(i).result_code() ==
        ResultCode::NO_VALID_HOSTNAME) {
      invalid_count++;
    } else if (response.connectivity_results(i).result_code() ==
               ResultCode::INTERNAL_ERROR) {
      internal_error_count++;
    }
  }
  EXPECT_EQ(invalid_count, 1);
  EXPECT_GE(internal_error_count, 1);
}

TEST_F(HostsConnectivityDiagnosticsTest, AllInvalidHostnamesCompleteRequest) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"192.168.1.1", "localhost"};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  // Both hostnames are invalid, no skeleton INTERNAL_ERROR should appear.
  ASSERT_EQ(response.connectivity_results_size(), 2);
  for (int i = 0; i < response.connectivity_results_size(); i++) {
    EXPECT_EQ(response.connectivity_results(i).result_code(),
              ResultCode::NO_VALID_HOSTNAME);
    EXPECT_EQ(response.connectivity_results(i).error_message(),
              kInvalidHostname);
  }
}

TEST_F(HostsConnectivityDiagnosticsTest, DirectProxyPassesThrough) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kDirect};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Direct proxy should pass through to RunConnectivityTests (skeleton).
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

// Detailed proxy URL validation is covered by IsValidProxyUrl unit tests in
// hosts_connectivity_diagnostics_util_test.cc. These integration tests verify
// that ValidateAndAssignProxy correctly wires up the validation result.
TEST_F(HostsConnectivityDiagnosticsTest, ValidCustomProxyPassesThrough) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {
      .mode = HostsConnectivityDiagnostics::ProxyMode::kCustom,
      .custom_url = "http://proxy.example.com:8080"};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Valid proxy should pass through to RunConnectivityTests (skeleton).
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

TEST_F(HostsConnectivityDiagnosticsTest, InvalidProxyIsRejected) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {
      .mode = HostsConnectivityDiagnostics::ProxyMode::kCustom,
      .custom_url = "invalid-proxy"};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::NO_VALID_PROXY);
  EXPECT_EQ(result.proxy(), "invalid-proxy");
  EXPECT_EQ(result.error_message(), kInvalidProxy);
}

TEST_F(HostsConnectivityDiagnosticsTest,
       InvalidProxyReturnsEarlyBeforeHostnameValidation) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"example1.com", "example2.com", "example3.com",
                                "example4.com", "example5.com"};
  request_info.proxy = {
      .mode = HostsConnectivityDiagnostics::ProxyMode::kCustom,
      .custom_url = "invalid-proxy"};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Invalid proxy returns early with a single NO_VALID_PROXY result,
  // regardless of how many hostnames were provided.
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::NO_VALID_PROXY);
  EXPECT_EQ(result.proxy(), "invalid-proxy");
  EXPECT_EQ(result.error_message(), kInvalidProxy);
}

TEST_F(HostsConnectivityDiagnosticsTest, SystemProxyPassesThrough) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // System proxy currently falls through to RunConnectivityTests (skeleton).
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

TEST_F(HostsConnectivityDiagnosticsTest, MultipleRequestsAreQueued) {
  base::test::TestFuture<const TestConnectivityResponse&> future1;
  base::test::TestFuture<const TestConnectivityResponse&> future2;

  HostsConnectivityDiagnostics::RequestInfo request_info1;
  request_info1.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info1.callback = future1.GetCallback();

  HostsConnectivityDiagnostics::RequestInfo request_info2;
  request_info2.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info2.callback = future2.GetCallback();

  diagnostics_->TestHostsConnectivity(std::move(request_info1));
  diagnostics_->TestHostsConnectivity(std::move(request_info2));

  // Both requests should complete with INTERNAL_ERROR (skeleton).
  const auto& response1 = future1.Get();
  ASSERT_EQ(response1.connectivity_results_size(), 1);
  EXPECT_EQ(response1.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);

  const auto& response2 = future2.Get();
  ASSERT_EQ(response2.connectivity_results_size(), 1);
  EXPECT_EQ(response2.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseTimeoutDefault) {
  KeyValueStore options;
  EXPECT_EQ(HostsConnectivityDiagnostics::ParseTimeout(options),
            base::Seconds(10));
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseTimeoutValues) {
  // {input_seconds, expected_seconds}: valid range is [1, 60], out-of-range
  // values fall back to the default of 10 seconds.
  constexpr std::array<std::pair<uint32_t, int>, 6> kTestCases = {{
      {1, 1},      // Minimum boundary.
      {30, 30},    // Mid-range valid value.
      {60, 60},    // Maximum boundary.
      {0, 10},     // Zero falls back to default.
      {61, 10},    // Exceeds max falls back to default.
      {1000, 10},  // Large value falls back to default.
  }};

  for (const auto& [input, expected] : kTestCases) {
    SCOPED_TRACE(input);
    KeyValueStore options;
    options.Set<uint32_t>(kTestHostsConnectivityTimeoutKey, input);
    EXPECT_EQ(HostsConnectivityDiagnostics::ParseTimeout(options),
              base::Seconds(expected));
  }
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseMaxErrorCountDefault) {
  KeyValueStore options;
  EXPECT_EQ(HostsConnectivityDiagnostics::ParseMaxErrorCount(options), 0u);
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseMaxErrorCountValues) {
  // {input, expected}: any uint32_t value is accepted as-is; 0 means no limit.
  constexpr std::array<std::pair<uint32_t, uint32_t>, 4> kTestCases = {{
      {0, 0},      // Explicit zero (no limit).
      {1, 1},      // Minimum meaningful limit.
      {5, 5},      // Typical limit.
      {100, 100},  // Large limit.
  }};

  for (const auto& [input, expected] : kTestCases) {
    SCOPED_TRACE(input);
    KeyValueStore options;
    options.Set<uint32_t>(kTestHostsConnectivityMaxErrorsKey, input);
    EXPECT_EQ(HostsConnectivityDiagnostics::ParseMaxErrorCount(options),
              expected);
  }
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseProxyOptionDefault) {
  KeyValueStore options;
  auto proxy = HostsConnectivityDiagnostics::ParseProxyOption(options);
  EXPECT_EQ(proxy.mode, HostsConnectivityDiagnostics::ProxyMode::kDirect);
  EXPECT_FALSE(proxy.custom_url.has_value());
}

TEST_F(HostsConnectivityDiagnosticsTest, ParseProxyOptionValues) {
  using ProxyMode = HostsConnectivityDiagnostics::ProxyMode;
  constexpr std::string_view kCustomProxy = "http://proxy.example.com:8080";
  // {input, expected_mode, expected_custom_url}.
  struct TestCase {
    std::string_view input;
    ProxyMode expected_mode;
    std::optional<std::string_view> expected_custom_url;
  };
  constexpr auto kTestCases = std::to_array<TestCase>({
      {kTestHostsConnectivityProxyDirect, ProxyMode::kDirect, std::nullopt},
      {kTestHostsConnectivityProxySystem, ProxyMode::kSystem, std::nullopt},
      {kCustomProxy, ProxyMode::kCustom, kCustomProxy},
  });

  for (const auto& [input, expected_mode, expected_custom_url] : kTestCases) {
    SCOPED_TRACE(input);
    KeyValueStore options;
    options.Set<std::string>(kTestHostsConnectivityProxyKey,
                             std::string(input));
    auto proxy = HostsConnectivityDiagnostics::ParseProxyOption(options);
    EXPECT_EQ(proxy.mode, expected_mode);
    if (expected_custom_url.has_value()) {
      ASSERT_TRUE(proxy.custom_url.has_value());
      EXPECT_EQ(proxy.custom_url.value(), expected_custom_url.value());
    } else {
      EXPECT_FALSE(proxy.custom_url.has_value());
    }
  }
}

}  // namespace
}  // namespace shill
