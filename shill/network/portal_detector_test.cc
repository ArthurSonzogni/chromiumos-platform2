// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/portal_detector.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/http_request.h"
#include "shill/mock_event_dispatcher.h"

using testing::_;
using testing::AnyOfArray;
using testing::Eq;
using testing::Field;
using testing::Mock;
using testing::Return;
using testing::Test;

namespace shill {

namespace {
const char kInterfaceName[] = "int0";
const net_base::HttpUrl kHttpUrl =
    *net_base::HttpUrl::CreateFromString("http://www.chromium.org");
const net_base::HttpUrl kHttpsUrl =
    *net_base::HttpUrl::CreateFromString("https://www.google.com");
const std::vector<net_base::HttpUrl> kFallbackHttpUrls{
    *net_base::HttpUrl::CreateFromString("http://www.google.com/gen_204"),
    *net_base::HttpUrl::CreateFromString(
        "http://play.googleapis.com/generate_204"),
};
const std::vector<net_base::HttpUrl> kFallbackHttpsUrls{
    *net_base::HttpUrl::CreateFromString("http://url1.com/gen204"),
    *net_base::HttpUrl::CreateFromString("http://url2.com/gen204"),
};
constexpr net_base::IPAddress kDNSServer0(net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kDNSServer1(net_base::IPv4Address(8, 8, 4, 4));
constexpr std::string_view kPortalSignInURL = "https://portal.com/login";

class MockHttpRequest : public HttpRequest {
 public:
  MockHttpRequest()
      : HttpRequest(nullptr,
                    kInterfaceName,
                    net_base::IPFamily::kIPv4,
                    {kDNSServer0, kDNSServer1},
                    true,
                    brillo::http::Transport::CreateDefault(),
                    nullptr) {}
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;
  ~MockHttpRequest() override = default;

  void Start(std::string_view logging_tag,
             const net_base::HttpUrl& url,
             const brillo::http::HeaderList& headers,
             base::OnceCallback<void(Result result)> callback) override {
    // We only verify the URL in the test.
    StartWithUrl(url);
  }

  MOCK_METHOD(void, StartWithUrl, (const net_base::HttpUrl&), ());
};

}  // namespace

class MockPatchpanelClient : public patchpanel::FakeClient {
 public:
  MockPatchpanelClient() = default;
  ~MockPatchpanelClient() = default;

  MOCK_METHOD(void,
              PrepareTagSocket,
              (const TrafficAnnotation&,
               std::shared_ptr<brillo::http::Transport>),
              (override));
};

class TestablePortalDetector : public PortalDetector {
 public:
  TestablePortalDetector(EventDispatcher* dispatcher,
                         patchpanel::Client* patchpanel_client,
                         const ProbingConfiguration& probing_configuration)
      : PortalDetector(dispatcher,
                       patchpanel_client,
                       kInterfaceName,
                       probing_configuration,
                       "tag") {}
  TestablePortalDetector(const TestablePortalDetector&) = delete;
  TestablePortalDetector& operator=(const TestablePortalDetector&) = delete;
  ~TestablePortalDetector() override = default;

  MOCK_METHOD(std::unique_ptr<HttpRequest>,
              CreateHTTPRequest,
              (const std::string& ifname,
               net_base::IPFamily ip_family,
               const std::vector<net_base::IPAddress>& dns_list,
               bool allow_non_google_https),
              (override, const));
};

class PortalDetectorTest : public Test {
 public:
  PortalDetectorTest()
      : http_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        http_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            http_probe_transport_)),
        https_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        https_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            https_probe_transport_)),
        interface_name_(kInterfaceName),
        dns_servers_({kDNSServer0, kDNSServer1}),
        portal_detector_(new TestablePortalDetector(
            &dispatcher_, &patchpanel_client_, MakeProbingConfiguration())) {}

 protected:
  class CallbackTarget {
   public:
    MOCK_METHOD(void, ResultCallback, (const PortalDetector::Result&));
  };

  static PortalDetector::ProbingConfiguration MakeProbingConfiguration() {
    return PortalDetector::ProbingConfiguration{
        .portal_http_url = kHttpUrl,
        .portal_https_url = kHttpsUrl,
        .portal_fallback_http_urls = kFallbackHttpUrls,
        .portal_fallback_https_urls = kFallbackHttpsUrls,
    };
  }

  PortalDetector::Result GetPortalRedirectResult(
      const net_base::HttpUrl& probe_url) {
    const PortalDetector::Result r = {
        .num_attempts = 1,
        .http_result = PortalDetector::ProbeResult::kPortalRedirect,
        .http_status_code = 302,
        .http_content_length = 0,
        .https_result = PortalDetector::ProbeResult::kConnectionFailure,
        .redirect_url = net_base::HttpUrl::CreateFromString(kPortalSignInURL),
        .probe_url = probe_url,
    };
    EXPECT_TRUE(r.IsHTTPProbeComplete());
    EXPECT_TRUE(r.IsHTTPSProbeComplete());
    EXPECT_EQ(PortalDetector::ValidationState::kPortalRedirect,
              r.GetValidationState());
    return r;
  }

  void StartPortalRequest() {
    // Expect that PortalDetector will create the request of the HTTP probe
    // first.
    EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
        .WillOnce([]() {
          auto http_request = std::make_unique<MockHttpRequest>();
          EXPECT_CALL(*http_request, StartWithUrl);
          return http_request;
        })
        .WillOnce([]() {
          auto https_request = std::make_unique<MockHttpRequest>();
          EXPECT_CALL(*https_request, StartWithUrl);
          return https_request;
        });
    EXPECT_CALL(callback_target_, ResultCallback).Times(0);
    portal_detector_->Start(
        /*http_only=*/false, net_base::IPFamily::kIPv4,
        {kDNSServer0, kDNSServer1},
        base::BindOnce(&CallbackTarget::ResultCallback,
                       base::Unretained(&callback_target_)));
    EXPECT_TRUE(portal_detector_->IsRunning());
    Mock::VerifyAndClearExpectations(&callback_target_);
  }

  void StartHTTPOnlyPortalRequest() {
    // Expect that PortalDetector will create the request of the HTTP probe
    // first.
    EXPECT_CALL(*portal_detector_, CreateHTTPRequest).WillOnce([]() {
      auto http_request = std::make_unique<MockHttpRequest>();
      EXPECT_CALL(*http_request, StartWithUrl);
      return http_request;
    });
    EXPECT_CALL(callback_target_, ResultCallback).Times(0);
    portal_detector_->Start(
        /*http_only=*/true, net_base::IPFamily::kIPv4,
        {kDNSServer0, kDNSServer1},
        base::BindOnce(&CallbackTarget::ResultCallback,
                       base::Unretained(&callback_target_)));
    EXPECT_TRUE(portal_detector_->IsRunning());
    Mock::VerifyAndClearExpectations(&callback_target_);
  }

  void ExpectReset() {
    EXPECT_EQ(0, portal_detector_->attempt_count());
    EXPECT_FALSE(portal_detector_->IsRunning());
  }

  void ExpectHttpRequestSuccessWithStatus(int status_code) {
    EXPECT_CALL(*http_probe_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));
    auto response =
        std::make_unique<brillo::http::Response>(http_probe_connection_);
    portal_detector_->ProcessHTTPProbeResult(kHttpUrl, base::TimeTicks(),
                                             std::move(response));
  }

  void HTTPSRequestSuccess() {
    auto response =
        std::make_unique<brillo::http::Response>(https_probe_connection_);
    portal_detector_->ProcessHTTPSProbeResult(base::TimeTicks(),
                                              std::move(response));
  }

  void HTTPRequestFailure(HttpRequest::Error error) {
    portal_detector_->ProcessHTTPProbeResult(kHttpUrl, base::TimeTicks(),
                                             base::unexpected(error));
  }

  void HTTPSRequestFailure(HttpRequest::Error error) {
    portal_detector_->ProcessHTTPSProbeResult(base::TimeTicks(),
                                              base::unexpected(error));
  }

 protected:
  MockEventDispatcher dispatcher_;
  std::shared_ptr<brillo::http::MockTransport> http_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> http_probe_connection_;
  std::shared_ptr<brillo::http::MockTransport> https_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> https_probe_connection_;
  CallbackTarget callback_target_;
  const std::string interface_name_;
  std::vector<net_base::IPAddress> dns_servers_;
  std::unique_ptr<TestablePortalDetector> portal_detector_;
  MockPatchpanelClient patchpanel_client_;
};

TEST_F(PortalDetectorTest, NoCustomCertificates) {
  std::vector<net_base::IPAddress> dns_list = {kDNSServer0, kDNSServer1};
  auto config = MakeProbingConfiguration();
  config.portal_https_url =
      *net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpsUrl);
  auto portal_detector = std::make_unique<TestablePortalDetector>(
      &dispatcher_, &patchpanel_client_, config);

  // First request for the HTTP probe: always set |allow_non_google_https| to
  // false. Second request for the HTTPS probe with the default URL: set
  // |allow_non_google_https| to false.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/false))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));

  portal_detector->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                         dns_list, base::DoNothing());
  portal_detector->Reset();
}

TEST_F(PortalDetectorTest, UseCustomCertificates) {
  std::vector<net_base::IPAddress> dns_list = {kDNSServer0, kDNSServer1};
  auto config = MakeProbingConfiguration();
  ASSERT_NE(config.portal_https_url, *net_base::HttpUrl::CreateFromString(
                                         PortalDetector::kDefaultHttpsUrl));
  auto portal_detector = std::make_unique<TestablePortalDetector>(
      &dispatcher_, &patchpanel_client_, config);

  // First request for the HTTP probe: always set |allow_non_google_https| to
  // false.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/false))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));
  // Second request for the HTTPS probe with a non-default URL: set
  // |allow_non_google_https| to true.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/true))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));

  portal_detector->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                         dns_list, base::DoNothing());
  portal_detector->Reset();
}

TEST_F(PortalDetectorTest, Constructor) {
  ExpectReset();
}

TEST_F(PortalDetectorTest, IsInProgress) {
  // Before the trial is started, should not be active.
  EXPECT_FALSE(portal_detector_->IsRunning());

  // Once the trial is started, IsInProgress should return true.
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  // Finish the trial, IsInProgress should return false.
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  portal_detector_->StopTrialIfComplete(result);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RestartAfterRedirect) {
  EXPECT_FALSE(portal_detector_->IsRunning());
  EXPECT_EQ(0, portal_detector_->attempt_count());

  // Start the 1st attempt that uses the default probing URLs.
  EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
      .WillOnce([]() {
        auto http_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*http_request, StartWithUrl(kHttpUrl));
        return http_request;
      })
      .WillOnce([]() {
        auto https_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*https_request, StartWithUrl(kHttpsUrl));
        return https_request;
      });
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1}, base::DoNothing());
  EXPECT_EQ(1, portal_detector_->attempt_count());

  // Receive the kPortalRedirect result.
  portal_detector_->StopTrialIfComplete(GetPortalRedirectResult(kHttpUrl));
  EXPECT_FALSE(portal_detector_->IsRunning());

  // After receiving the kPortalRedirect result, we reuse the same HTTP URL at
  // the following attempt.
  EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
      .WillOnce([]() {
        auto http_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*http_request, StartWithUrl(kHttpUrl));
        return http_request;
      })
      .WillOnce([]() {
        auto https_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*https_request, StartWithUrl);
        return https_request;
      });
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1}, base::DoNothing());
  EXPECT_EQ(2, portal_detector_->attempt_count());

  portal_detector_->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartAfterSuspectedRedirect) {
  // Start the 1st attempt that uses the default probing URLs.
  EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
      .WillOnce([]() {
        auto http_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*http_request, StartWithUrl(kHttpUrl));
        return http_request;
      })
      .WillOnce([]() {
        auto https_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*https_request, StartWithUrl(kHttpsUrl));
        return https_request;
      });
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1}, base::DoNothing());

  // Receive the kPortalSuspected result.
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 345,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .probe_url = kHttpUrl,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  portal_detector_->StopTrialIfComplete(result);
  EXPECT_FALSE(portal_detector_->IsRunning());

  // After receiving the kPortalSuspected result, we reuse the same HTTP URL.
  EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
      .WillOnce([]() {
        auto http_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*http_request, StartWithUrl(kHttpUrl));
        return http_request;
      })
      .WillOnce([]() {
        auto https_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*https_request, StartWithUrl);
        return https_request;
      });
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1}, base::DoNothing());
}

TEST_F(PortalDetectorTest, RestartWhileAlreadyInProgress) {
  EXPECT_FALSE(portal_detector_->IsRunning());

  EXPECT_EQ(0, portal_detector_->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(1, portal_detector_->attempt_count());
  EXPECT_TRUE(portal_detector_->IsRunning());
  Mock::VerifyAndClearExpectations(portal_detector_.get());

  EXPECT_CALL(*portal_detector_, CreateHTTPRequest).Times(0);
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1}, base::DoNothing());
  EXPECT_EQ(1, portal_detector_->attempt_count());
  EXPECT_TRUE(portal_detector_->IsRunning());
  Mock::VerifyAndClearExpectations(portal_detector_.get());

  portal_detector_->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, AttemptCount) {
  PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kDNSFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  // The 1st attempt uses the default probing URLs.
  EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
      .WillOnce([]() {
        auto http_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*http_request, StartWithUrl(kHttpUrl));
        return http_request;
      })
      .WillOnce([]() {
        auto https_request = std::make_unique<MockHttpRequest>();
        EXPECT_CALL(*https_request, StartWithUrl(kHttpsUrl));
        return https_request;
      });
  portal_detector_->Start(/*http_only=*/false, net_base::IPFamily::kIPv4,
                          {kDNSServer0, kDNSServer1},
                          base::BindOnce(&CallbackTarget::ResultCallback,
                                         base::Unretained(&callback_target_)));

  result.num_attempts = 1;
  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  portal_detector_->StopTrialIfComplete(result);
  EXPECT_EQ(1, portal_detector_->attempt_count());

  // The 2nd and so on attempts use the fallback or the default probing URLs.
  std::vector<net_base::HttpUrl> expected_retry_http_urls = kFallbackHttpUrls;
  expected_retry_http_urls.push_back(kHttpUrl);
  std::vector<net_base::HttpUrl> expected_retry_https_urls = kFallbackHttpsUrls;
  expected_retry_https_urls.push_back(kHttpsUrl);
  for (int i = 2; i < 10; i++) {
    result.num_attempts = i;
    EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));

    EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
        .WillOnce([&expected_retry_http_urls]() {
          auto http_request = std::make_unique<MockHttpRequest>();
          EXPECT_CALL(*http_request,
                      StartWithUrl(AnyOfArray(expected_retry_http_urls)))
              .Times(1);
          return http_request;
        })
        .WillOnce([&expected_retry_https_urls]() {
          auto https_request = std::make_unique<MockHttpRequest>();
          EXPECT_CALL(*https_request,
                      StartWithUrl(AnyOfArray(expected_retry_https_urls)))
              .Times(1);
          return https_request;
        });

    portal_detector_->Start(
        /*http_only=*/false, net_base::IPFamily::kIPv4,
        {kDNSServer0, kDNSServer1},
        base::BindOnce(&CallbackTarget::ResultCallback,
                       base::Unretained(&callback_target_)));
    EXPECT_EQ(i, portal_detector_->attempt_count());

    portal_detector_->StopTrialIfComplete(result);
    Mock::VerifyAndClearExpectations(&callback_target_);
  }

  portal_detector_->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RequestSuccess) {
  StartPortalRequest();

  // HTTPS probe does not trigger anything (for now)
  EXPECT_CALL(callback_target_, ResultCallback).Times(0);
  HTTPSRequestSuccess();
  Mock::VerifyAndClearExpectations(&callback_target_);

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(204);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 123,
      .http_content_length = 10,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("10"));
  ExpectHttpRequestSuccessWithStatus(123);
  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  HTTPSRequestSuccess();
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestHTTPSuccessHTTPSFailure) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kTLSFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(204);
  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  HTTPSRequestFailure(HttpRequest::Error::kTLSFailure);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestFail) {
  StartPortalRequest();

  // HTTPS probe does not trigger anything (for now)
  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 123,
      .http_content_length = 10,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("10"));
  ExpectHttpRequestSuccessWithStatus(123);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestRedirect) {
  StartPortalRequest();

  EXPECT_CALL(callback_target_, ResultCallback).Times(0);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  Mock::VerifyAndClearExpectations(&callback_target_);

  EXPECT_CALL(callback_target_,
              ResultCallback(Eq(GetPortalRedirectResult(kHttpUrl))));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestTempRedirect) {
  StartPortalRequest();

  EXPECT_CALL(callback_target_, ResultCallback).Times(0);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  Mock::VerifyAndClearExpectations(&callback_target_);

  PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  result.http_status_code = 307;
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(307);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestRedirectWithHTTPSProbeTimeout) {
  StartPortalRequest();

  PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  result.https_result = PortalDetector::ProbeResult::kNoResult;
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_FALSE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  // The HTTPS probe does not complete.
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, Request200AndInvalidContentLength) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 200,
      .http_content_length = std::nullopt,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("invalid"));
  ExpectHttpRequestSuccessWithStatus(200);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, Request200WithoutContent) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 200,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(200);
  HTTPSRequestSuccess();
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, Request200WithContent) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 768,
      .probe_url = kHttpUrl,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_FALSE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("768"));
  ExpectHttpRequestSuccessWithStatus(200);
  // The trial has been completed, even if the HTTPS probe did not complete.
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, RequestInvalidRedirect) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kTLSFailure,
      .redirect_url = std::nullopt,
      .probe_url = kHttpUrl,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return("invalid_url"));
  ExpectHttpRequestSuccessWithStatus(302);
  HTTPSRequestFailure(HttpRequest::Error::kTLSFailure);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, HTTPOnlyRequestSuccess) {
  StartHTTPOnlyPortalRequest();

  const PortalDetector::Result result = {
      .http_only = true,
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
  };
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(204);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, HTTPOnlyRequestRedirect) {
  StartHTTPOnlyPortalRequest();

  const PortalDetector::Result result = {
      .http_only = true,
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = kHttpUrl,
  };
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return(std::string("https://portal.com/login")));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, HTTPOnlyRequestPortalSuspected) {
  StartHTTPOnlyPortalRequest();

  const PortalDetector::Result result = {
      .http_only = true,
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 456,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url = std::nullopt,
      .probe_url = kHttpUrl,
  };
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("456"));
  ExpectHttpRequestSuccessWithStatus(200);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, HTTPOnlyRequestInvalidRedirect) {
  StartHTTPOnlyPortalRequest();

  const PortalDetector::Result result = {
      .http_only = true,
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url = std::nullopt,
      .probe_url = kHttpUrl,
  };
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Location"))
      .WillOnce(Return(""));
  EXPECT_CALL(*http_probe_connection_, GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, HTTPOnlyRequestFailure) {
  StartHTTPOnlyPortalRequest();

  const PortalDetector::Result result = {
      .http_only = true,
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kNoResult,
  };
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target_, ResultCallback(Eq(result)));
  HTTPRequestFailure(HttpRequest::Error::kConnectionFailure);
  EXPECT_FALSE(portal_detector_->IsRunning());
}

TEST_F(PortalDetectorTest, PickProbeURLs) {
  const auto url1 = *net_base::HttpUrl::CreateFromString("http://www.url1.com");
  const auto url2 = *net_base::HttpUrl::CreateFromString("http://www.url2.com");
  const auto url3 = *net_base::HttpUrl::CreateFromString("http://www.url3.com");
  const std::set<std::string> all_urls = {url1.ToString(), url2.ToString(),
                                          url3.ToString()};
  std::set<std::string> all_found_urls;

  EXPECT_EQ(url1, portal_detector_->PickProbeUrl(url1, {}));
  EXPECT_EQ(url1, portal_detector_->PickProbeUrl(url1, {url2, url3}));

  // The loop index starts at 2 to force |attempt_count_| > 1 and simulate
  // attempts after the first attempts and force using the fallback list.
  for (int i = 2; i < 100; i++) {
    portal_detector_->attempt_count_ = i;
    EXPECT_EQ(portal_detector_->PickProbeUrl(url1, {}), url1);

    const auto& found =
        portal_detector_->PickProbeUrl(url1, {url2, url3}).ToString();
    if (i == 2) {
      EXPECT_EQ(url2.ToString(), found);
    } else if (i == 3) {
      EXPECT_EQ(url3.ToString(), found);
    } else {
      all_found_urls.insert(found);
    }
    EXPECT_NE(all_urls.find(found), all_urls.end());
  }
  // Probability this assert fails = 3 * 1/3 ^ 97 + 3 * 2/3 ^ 97
  EXPECT_EQ(all_urls, all_found_urls);
}

TEST_F(PortalDetectorTest, CreateHTTPRequest) {
  PortalDetector detector(&dispatcher_, &patchpanel_client_, kInterfaceName,
                          MakeProbingConfiguration(), "tag");

  EXPECT_CALL(
      patchpanel_client_,
      PrepareTagSocket(
          Field(&patchpanel::Client::TrafficAnnotation::id,
                patchpanel::Client::TrafficAnnotationId::kShillPortalDetector),
          _));

  auto req =
      detector.CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                                 {kDNSServer0, kDNSServer1}, true);
  EXPECT_TRUE(req != nullptr);
}

TEST(PortalDetectorResultTest, HTTPSTimeout) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kHTTPTimeout,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

TEST(PortalDetectorResultTest, PartialConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

TEST(PortalDetectorResultTest, NoConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultConnectionFailure);
}

TEST(PortalDetectorResultTest, InternetConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultOnline);
}

TEST(PortalDetectorResultTest, PortalRedirect) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalRedirect);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectFound);
}

TEST(PortalDetectorResultTest, PortalInvalidRedirect) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .redirect_url = std::nullopt,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectNoUrl);
}

TEST(PortalDetectorResultTest, Empty200) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 200,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultOnline);
}

TEST(PortalDetectorResultTest, PortalSuspected200) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 1023,
      .https_result = PortalDetector::ProbeResult::kTLSFailure,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalSuspected);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

TEST(PortalDetectorResultTest, HTTPOnlySuccessfulProbe) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultOnline);
}

TEST(PortalDetectorResultTest, HTTPOnlyDNSFailure) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kDNSFailure,
      .https_result = PortalDetector::ProbeResult::kNoResult,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultDNSFailure);
}

TEST(PortalDetectorResultTest, HTTPOnlyConnectionFailure) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kNoResult,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultConnectionFailure);
}

TEST(PortalDetectorResultTest, HTTPOnlyPortalRedirect) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalRedirect);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectFound);
}

TEST(PortalDetectorResultTest, HTTPOnlyPortalInvalidRedirect) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url = std::nullopt,
      .probe_url = std::nullopt,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectNoUrl);
}

TEST(PortalDetectorResultTest, HTTPOnlyPortalSuspected) {
  const PortalDetector::Result result = {
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 346,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalSuspected);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectFound);
}

}  // namespace shill
