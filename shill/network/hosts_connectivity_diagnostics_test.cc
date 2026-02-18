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
#include <base/task/single_thread_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport_error.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/ip_address.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

#include "shill/network/http_request.h"
#include "shill/store/key_value_store.h"

namespace shill {
namespace {

using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;
using TestConnectivityResponse =
    hosts_connectivity_diagnostics::TestConnectivityResponse;

constexpr std::string_view kExampleDotCom = "example.com";
constexpr std::string_view kHttpsExampleDotCom = "https://example.com";
constexpr char kLoggingTag[] = "test_logging_tag";
constexpr char kInterfaceName[] = "test0";
constexpr net_base::IPAddress kDNSServer(net_base::IPv4Address(8, 8, 8, 8));

// MockHttpRequest provides two modes of operation:
//
// 1. Deferred mode (default): Start() captures the callback; the test calls
//    CompleteWithSuccess() or CompleteWithError() to drive the result.
//
// 2. Auto-complete mode: when `auto_result` is set at construction, Start()
//    immediately invokes the callback with that result. Useful when multiple
//    sequential requests all produce the same outcome.
class MockHttpRequest : public HttpRequest {
 public:
  // Constructs a deferred-mode MockHttpRequest. The test must call
  // CompleteWithSuccess() or CompleteWithError() after Start() is called.
  MockHttpRequest()
      : HttpRequest(nullptr,
                    kInterfaceName,
                    net_base::IPFamily::kIPv4,
                    {kDNSServer},
                    /*allow_non_google_https=*/true,
                    brillo::http::Transport::CreateDefault(),
                    /*dns_client_factory=*/nullptr),
        auto_error_(std::nullopt),
        auto_success_(false) {}
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;
  ~MockHttpRequest() override = default;

  // Marks this request to auto-complete with SUCCESS when Start() is called.
  void SetAutoSuccess() { auto_success_ = true; }

  // Marks this request to auto-complete with `error` when Start() is called.
  void SetAutoError(brillo::http::TransportError error) { auto_error_ = error; }

  // Captures the callback for later invocation, or posts it asynchronously in
  // auto-complete mode. Also records `method` and `timeout` for verification.
  void Start(Method method,
             std::string_view logging_tag,
             const net_base::HttpUrl& url,
             const brillo::http::HeaderList& headers,
             base::OnceCallback<void(Result result)> callback,
             std::optional<base::TimeDelta> timeout = std::nullopt,
             std::optional<RetryPolicy> retry_policy = std::nullopt) override {
    last_method_ = method;
    last_timeout_ = timeout;
    if (auto_error_.has_value()) {
      auto error = *auto_error_;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback), base::unexpected(error)));
      return;
    }
    if (auto_success_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              std::move(callback),
              base::ok(std::make_unique<brillo::http::Response>(nullptr))));
      return;
    }
    pending_callback_ = std::move(callback);
  }

  // Returns the method passed to the most recent Start() call.
  Method last_method() const { return last_method_; }

  // Returns the timeout passed to the most recent Start() call.
  std::optional<base::TimeDelta> last_timeout() const { return last_timeout_; }

  MOCK_METHOD(void, Stop, (), (override));

  // Invokes the captured callback with a success result. Only valid in
  // deferred mode (after Start() was called without auto-complete).
  void CompleteWithSuccess() {
    ASSERT_TRUE(pending_callback_);
    std::move(pending_callback_)
        .Run(base::ok(std::make_unique<brillo::http::Response>(nullptr)));
  }

  // Invokes the captured callback with `error`. Only valid in deferred mode.
  void CompleteWithError(brillo::http::TransportError error) {
    ASSERT_TRUE(pending_callback_);
    std::move(pending_callback_).Run(base::unexpected(error));
  }

  bool HasPendingCallback() const { return !!pending_callback_; }

 private:
  std::optional<brillo::http::TransportError> auto_error_;
  bool auto_success_;
  base::OnceCallback<void(Result)> pending_callback_;
  Method last_method_ = Method::kGet;
  std::optional<base::TimeDelta> last_timeout_;
};

// Returns an HttpRequestFactory that produces auto-success MockHttpRequests.
HostsConnectivityDiagnostics::HttpRequestFactory MakeAutoSuccessFactory() {
  return base::BindRepeating([](std::shared_ptr<brillo::http::Transport>)
                                 -> std::unique_ptr<HttpRequest> {
    auto req = std::make_unique<MockHttpRequest>();
    req->SetAutoSuccess();
    return req;
  });
}

class HostsConnectivityDiagnosticsTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(std::move(options));
    diagnostics_ = std::make_unique<HostsConnectivityDiagnostics>(
        /*dispatcher=*/nullptr, mock_bus_, kLoggingTag);
  }

  // Sets up system proxy resolution for testing.
  void SetupSystemProxyResolution(bool success,
                                  const std::vector<std::string>& proxies) {
    diagnostics_->SetGetProxyFunctionForTest(base::BindRepeating(
        [](bool success, std::vector<std::string> proxies, const std::string&,
           HostsConnectivityDiagnostics::GetProxyCallback callback) {
          std::move(callback).Run(success, proxies);
        },
        success, proxies));
  }

  // Installs a factory that stores each new MockHttpRequest in
  // `last_mock_request_`. The test controls completion via
  // last_mock_request_->CompleteWithSuccess() / CompleteWithError().
  void SetupDeferredMockHttpRequest() {
    diagnostics_->SetHttpRequestFactoryForTest(base::BindRepeating(
        [](HostsConnectivityDiagnosticsTest* self,
           std::shared_ptr<brillo::http::Transport>)
            -> std::unique_ptr<HttpRequest> {
          auto req = std::make_unique<MockHttpRequest>();
          self->last_mock_request_ = req.get();
          return req;
        },
        this));
  }

  // Populates `request_info` with default network context for tests.
  void SetNetworkContext(
      HostsConnectivityDiagnostics::RequestInfo& request_info) {
    request_info.interface_name = std::string(kInterfaceName);
    request_info.ip_family = net_base::IPFamily::kIPv4;
    request_info.dns_list = {kDNSServer};
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  scoped_refptr<dbus::MockBus> mock_bus_;
  std::unique_ptr<HostsConnectivityDiagnostics> diagnostics_;
  // Points to the most recently created MockHttpRequest (owned by
  // diagnostics_).
  MockHttpRequest* last_mock_request_ = nullptr;
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
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(kExampleDotCom);
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  // Should normalize to https://example.com.
  EXPECT_EQ(response.connectivity_results(0).hostname(), kHttpsExampleDotCom);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::SUCCESS);
}

TEST_F(HostsConnectivityDiagnosticsTest, HttpsHostnameIsAccepted) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(kHttpsExampleDotCom);
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).hostname(), kHttpsExampleDotCom);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::SUCCESS);
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
  diagnostics_->SetHttpRequestFactoryForTest(MakeAutoSuccessFactory());

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"https://valid.com", "192.168.1.1",
                                "another-valid.com"};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  // Should have 3 results: 2 valid hostnames (SUCCESS) + 1 invalid (IP).
  ASSERT_EQ(response.connectivity_results_size(), 3);

  int valid_count = 0;
  int invalid_count = 0;
  for (int i = 0; i < response.connectivity_results_size(); i++) {
    if (response.connectivity_results(i).result_code() ==
        ResultCode::NO_VALID_HOSTNAME) {
      invalid_count++;
    } else if (response.connectivity_results(i).result_code() ==
               ResultCode::SUCCESS) {
      valid_count++;
    }
  }
  EXPECT_EQ(valid_count, 2);
  EXPECT_EQ(invalid_count, 1);
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
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kDirect};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::SUCCESS);
  EXPECT_EQ(result.hostname(), kHttpsExampleDotCom);
  // Direct proxy: proxy field should be empty.
  EXPECT_TRUE(result.proxy().empty());
  EXPECT_TRUE(result.error_message().empty());
  EXPECT_TRUE(result.has_timestamp_start());
  EXPECT_TRUE(result.has_timestamp_end());
}

// Detailed proxy URL validation is covered by IsValidProxyUrl unit tests in
// hosts_connectivity_diagnostics_util_test.cc. These integration tests verify
// that ValidateAndAssignProxy correctly wires up the validation result.
TEST_F(HostsConnectivityDiagnosticsTest, ValidCustomProxyPassesThrough) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {
      .mode = HostsConnectivityDiagnostics::ProxyMode::kCustom,
      .custom_url = "http://proxy.example.com:8080"};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::SUCCESS);
  EXPECT_EQ(result.hostname(), kHttpsExampleDotCom);
  EXPECT_EQ(result.proxy(), "http://proxy.example.com:8080");
  EXPECT_TRUE(result.error_message().empty());
  EXPECT_TRUE(result.has_timestamp_start());
  EXPECT_TRUE(result.has_timestamp_end());
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

TEST_F(HostsConnectivityDiagnosticsTest, SystemProxyResolutionSuccess) {
  constexpr char kProxy[] = "http://system-proxy:8080";
  SetupSystemProxyResolution(true, {kProxy});
  SetupDeferredMockHttpRequest();

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::SUCCESS);
  EXPECT_EQ(result.hostname(), kHttpsExampleDotCom);
  EXPECT_EQ(result.proxy(), kProxy);
  EXPECT_TRUE(result.error_message().empty());
  EXPECT_TRUE(result.has_timestamp_start());
  EXPECT_TRUE(result.has_timestamp_end());
}

TEST_F(HostsConnectivityDiagnosticsTest, SystemProxyResolutionFailure) {
  SetupSystemProxyResolution(false, {});

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Failed proxy resolution produces a NO_VALID_PROXY entry. No specs
  // reach RunConnectivityTests, so only the proxy error is returned.
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::NO_VALID_PROXY);
  EXPECT_EQ(result.hostname(), kHttpsExampleDotCom);
  EXPECT_EQ(result.error_message(), kUnableToGetSystemProxy);
  EXPECT_EQ(result.resolution_message(), kUnableToGetSystemProxyResolution);
}

TEST_F(HostsConnectivityDiagnosticsTest, SystemProxyMultipleHostnames) {
  SetupSystemProxyResolution(true, {"http://proxy:8080"});
  diagnostics_->SetHttpRequestFactoryForTest(MakeAutoSuccessFactory());

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"example1.com", "example2.com"};
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Both hostnames resolve proxies successfully. Each gets 1 proxy,
  // so 2 hostnames * 1 proxy = 2 results.
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 2);
  for (int i = 0; i < response.connectivity_results_size(); i++) {
    EXPECT_EQ(response.connectivity_results(i).result_code(),
              ResultCode::SUCCESS);
  }
}

TEST_F(HostsConnectivityDiagnosticsTest, SystemProxyPartialFailure) {
  // Alternate success/failure per call.
  int call_count = 0;
  diagnostics_->SetGetProxyFunctionForTest(base::BindRepeating(
      [](int* count, const std::string&,
         HostsConnectivityDiagnostics::GetProxyCallback callback) {
        (*count)++;
        if (*count % 2 == 0) {
          std::move(callback).Run(false, {});
        } else {
          std::move(callback).Run(true, {"http://proxy:8080"});
        }
      },
      &call_count));
  diagnostics_->SetHttpRequestFactoryForTest(MakeAutoSuccessFactory());

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"example1.com", "example2.com"};
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // ResolveNextSystemProxy pops from back: example2.com resolves first
  // (call 1, success), then example1.com (call 2, failure). The failed
  // hostname produces a NO_VALID_PROXY entry; the successful one gets a
  // SUCCESS connectivity result.
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 2);

  // First result is the proxy resolution failure (added during resolution).
  const auto& fail_result = response.connectivity_results(0);
  EXPECT_EQ(fail_result.result_code(), ResultCode::NO_VALID_PROXY);
  EXPECT_EQ(fail_result.error_message(), kUnableToGetSystemProxy);

  // Second result is the successful connectivity test.
  const auto& success_result = response.connectivity_results(1);
  EXPECT_EQ(success_result.result_code(), ResultCode::SUCCESS);
}

TEST_F(HostsConnectivityDiagnosticsTest, MultipleRequestsAreQueued) {
  diagnostics_->SetHttpRequestFactoryForTest(MakeAutoSuccessFactory());

  base::test::TestFuture<const TestConnectivityResponse&> future1;
  base::test::TestFuture<const TestConnectivityResponse&> future2;

  HostsConnectivityDiagnostics::RequestInfo request_info1;
  request_info1.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info1.callback = future1.GetCallback();
  SetNetworkContext(request_info1);

  HostsConnectivityDiagnostics::RequestInfo request_info2;
  request_info2.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info2.callback = future2.GetCallback();
  SetNetworkContext(request_info2);

  diagnostics_->TestHostsConnectivity(std::move(request_info1));
  diagnostics_->TestHostsConnectivity(std::move(request_info2));

  // Both requests should complete with SUCCESS.
  const auto& response1 = future1.Get();
  ASSERT_EQ(response1.connectivity_results_size(), 1);
  EXPECT_EQ(response1.connectivity_results(0).result_code(),
            ResultCode::SUCCESS);

  const auto& response2 = future2.Get();
  ASSERT_EQ(response2.connectivity_results_size(), 1);
  EXPECT_EQ(response2.connectivity_results(0).result_code(),
            ResultCode::SUCCESS);
}

TEST_F(HostsConnectivityDiagnosticsTest, ValidHostnameReturnsSuccess) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  last_mock_request_->CompleteWithSuccess();

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  const auto& result = response.connectivity_results(0);
  EXPECT_EQ(result.result_code(), ResultCode::SUCCESS);
  EXPECT_EQ(result.hostname(), kHttpsExampleDotCom);
  // Direct proxy (default): proxy field should be empty.
  EXPECT_TRUE(result.proxy().empty());
  EXPECT_TRUE(result.error_message().empty());
  EXPECT_TRUE(result.has_timestamp_start());
  EXPECT_TRUE(result.has_timestamp_end());
}

TEST_F(HostsConnectivityDiagnosticsTest, HttpRequestUsesHeadMethod) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  EXPECT_EQ(last_mock_request_->last_method(), HttpRequest::Method::kHead);
  last_mock_request_->CompleteWithSuccess();
  ASSERT_TRUE(future.Wait());
}

TEST_F(HostsConnectivityDiagnosticsTest, TimeoutIsForwardedToHttpRequest) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  constexpr base::TimeDelta kTimeout = base::Seconds(15);
  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  request_info.timeout = kTimeout;
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  ASSERT_TRUE(last_mock_request_->last_timeout().has_value());
  EXPECT_EQ(last_mock_request_->last_timeout().value(), kTimeout);
  last_mock_request_->CompleteWithSuccess();
  ASSERT_TRUE(future.Wait());
}

TEST_F(HostsConnectivityDiagnosticsTest, ZeroTimeoutIsPassedAsNullopt) {
  SetupDeferredMockHttpRequest();
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
  // Default timeout is zero — should be forwarded as nullopt.
  request_info.timeout = base::TimeDelta();
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  ASSERT_TRUE(last_mock_request_);
  EXPECT_FALSE(last_mock_request_->last_timeout().has_value());
  last_mock_request_->CompleteWithSuccess();
  ASSERT_TRUE(future.Wait());
}

TEST_F(HostsConnectivityDiagnosticsTest, MultipleHostnamesWithSystemProxies) {
  SetupSystemProxyResolution(true,
                             {"http://proxy1:8080", "http://proxy2:8080"});
  diagnostics_->SetHttpRequestFactoryForTest(MakeAutoSuccessFactory());

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"https://example1.com", "https://example2.com"};
  request_info.proxy = {.mode =
                            HostsConnectivityDiagnostics::ProxyMode::kSystem};
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  // Each hostname gets 2 proxies, so 2 hostnames * 2 proxies = 4 results.
  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 4);
}

TEST_F(HostsConnectivityDiagnosticsTest, TransportErrorMapsToResultCode) {
  using TE = brillo::http::TransportError;

  struct TestCase {
    TE error;
    ResultCode expected_code;
  };
  const std::vector<TestCase> kTestCases = {
      {TE::kDnsFailure, ResultCode::DNS_RESOLUTION_ERROR},
      {TE::kDnsTimeout, ResultCode::DNS_RESOLUTION_ERROR},
      {TE::kConnectionFailure, ResultCode::CONNECTION_FAILURE},
      {TE::kTimeout, ResultCode::CONNECTION_TIMEOUT},
      {TE::kTLSFailure, ResultCode::SSL_CONNECTION_ERROR},
  };

  for (const auto& tc : kTestCases) {
    SCOPED_TRACE(static_cast<int>(tc.error));
    SetupDeferredMockHttpRequest();
    base::test::TestFuture<const TestConnectivityResponse&> future;

    HostsConnectivityDiagnostics::RequestInfo request_info;
    request_info.raw_hostnames.emplace_back(std::string(kExampleDotCom));
    request_info.callback = future.GetCallback();
    SetNetworkContext(request_info);
    diagnostics_->TestHostsConnectivity(std::move(request_info));

    ASSERT_TRUE(last_mock_request_);
    last_mock_request_->CompleteWithError(tc.error);

    const auto& response = future.Get();
    ASSERT_EQ(response.connectivity_results_size(), 1);
    EXPECT_EQ(response.connectivity_results(0).result_code(), tc.expected_code);
    EXPECT_FALSE(response.connectivity_results(0).error_message().empty());
  }
}

TEST_F(HostsConnectivityDiagnosticsTest, MaxErrorCountAbortsEarly) {
  // Two hostnames, max_error_count=1 — should abort after first error.
  int request_count = 0;
  diagnostics_->SetHttpRequestFactoryForTest(base::BindRepeating(
      [](int* count, std::shared_ptr<brillo::http::Transport>)
          -> std::unique_ptr<HttpRequest> {
        (*count)++;
        auto req = std::make_unique<MockHttpRequest>();
        req->SetAutoError(brillo::http::TransportError::kConnectionFailure);
        return req;
      },
      &request_count));

  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames = {"example1.com", "example2.com"};
  request_info.max_error_count = 1;
  request_info.callback = future.GetCallback();
  SetNetworkContext(request_info);
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  // Only 1 result: aborted after the first error.
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::CONNECTION_FAILURE);
  EXPECT_EQ(request_count, 1);
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
