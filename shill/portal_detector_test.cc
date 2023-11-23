// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/http_request.h"
#include "shill/mock_event_dispatcher.h"

using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kInterfaceName[] = "int0";
const char kHttpUrl[] = "http://www.chromium.org";
const char kHttpsUrl[] = "https://www.google.com";
const std::vector<std::string> kFallbackHttpUrls{
    "http://www.google.com/gen_204",
    "http://play.googleapis.com/generate_204",
};
const std::vector<std::string> kFallbackHttpsUrls{
    "http://url1.com/gen204",
    "http://url2.com/gen204",
};
constexpr net_base::IPAddress kDNSServer0(net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kDNSServer1(net_base::IPv4Address(8, 8, 4, 4));

class MockHttpRequest : public HttpRequest {
 public:
  MockHttpRequest()
      : HttpRequest(nullptr,
                    kInterfaceName,
                    net_base::IPFamily::kIPv4,
                    {kDNSServer0, kDNSServer1},
                    true) {}
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;
  ~MockHttpRequest() = default;

  MOCK_METHOD(
      std::optional<HttpRequest::Error>,
      Start,
      (const std::string&,
       const net_base::HttpUrl&,
       const brillo::http::HeaderList&,
       base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>,
       base::OnceCallback<void(HttpRequest::Error)>),
      (override));
};

}  // namespace

MATCHER(PositiveDelay, "") {
  return arg.is_positive();
}

MATCHER(ZeroDelay, "") {
  return arg.is_zero();
}

MATCHER_P(IsResult, result, "") {
  return (result.http_phase == arg.http_phase &&
          result.http_status == arg.http_status &&
          result.https_phase == arg.https_phase &&
          result.https_status == arg.https_status &&
          result.redirect_url == arg.redirect_url &&
          result.probe_url == arg.probe_url);
}

class PortalDetectorTest : public Test {
 public:
  PortalDetectorTest()
      : http_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        http_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            http_probe_transport_)),
        https_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        https_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            https_probe_transport_)),
        http_request_(nullptr),
        https_request_(nullptr),
        interface_name_(kInterfaceName),
        dns_servers_({kDNSServer0, kDNSServer1}),
        portal_detector_(
            new PortalDetector(&dispatcher_,
                               MakeProbingConfiguration(),
                               callback_target_.result_callback())) {}

 protected:
  static const int kNumAttempts;

  class CallbackTarget {
   public:
    CallbackTarget()
        : result_callback_(base::BindRepeating(&CallbackTarget::ResultCallback,
                                               base::Unretained(this))) {}

    MOCK_METHOD(void, ResultCallback, (const PortalDetector::Result&));

    base::RepeatingCallback<void(const PortalDetector::Result&)>&
    result_callback() {
      return result_callback_;
    }

   private:
    base::RepeatingCallback<void(const PortalDetector::Result&)>
        result_callback_;
  };

  void AssignHttpRequest() {
    http_request_ = new StrictMock<MockHttpRequest>();
    https_request_ = new StrictMock<MockHttpRequest>();
    // Passes ownership.
    portal_detector_->http_request_.reset(http_request_);
    portal_detector_->https_request_.reset(https_request_);
  }

  static PortalDetector::ProbingConfiguration MakeProbingConfiguration() {
    PortalDetector::ProbingConfiguration config;
    config.portal_http_url = *net_base::HttpUrl::CreateFromString(kHttpUrl);
    config.portal_https_url = *net_base::HttpUrl::CreateFromString(kHttpsUrl);
    for (const auto& url : kFallbackHttpUrls) {
      config.portal_fallback_http_urls.push_back(
          *net_base::HttpUrl::CreateFromString(url));
    }
    for (const auto& url : kFallbackHttpsUrls) {
      config.portal_fallback_https_urls.push_back(
          *net_base::HttpUrl::CreateFromString(url));
    }
    return config;
  }

  void StartPortalRequest() {
    portal_detector_->Start(kInterfaceName, net_base::IPFamily::kIPv4,
                            {kDNSServer0, kDNSServer1}, "tag");
    AssignHttpRequest();
  }

  void StartTrialTask() {
    EXPECT_CALL(*http_request(), Start).WillOnce(Return(std::nullopt));
    EXPECT_CALL(*https_request(), Start).WillOnce(Return(std::nullopt));
    portal_detector()->StartTrialTask();
  }

  MockHttpRequest* http_request() { return http_request_; }
  MockHttpRequest* https_request() { return https_request_; }
  PortalDetector* portal_detector() { return portal_detector_.get(); }
  MockEventDispatcher& dispatcher() { return dispatcher_; }
  CallbackTarget& callback_target() { return callback_target_; }
  brillo::http::MockConnection* http_connection() {
    return http_probe_connection_.get();
  }
  brillo::http::MockConnection* https_connection() {
    return https_probe_connection_.get();
  }

  void ExpectReset() {
    EXPECT_EQ(0, portal_detector_->attempt_count_);
    EXPECT_TRUE(callback_target_.result_callback() ==
                portal_detector_->portal_result_callback_);
    ExpectCleanupTrial();
  }

  void ExpectCleanupTrial() {
    EXPECT_FALSE(portal_detector_->IsInProgress());
    EXPECT_FALSE(portal_detector_->IsTrialScheduled());
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
    EXPECT_EQ(nullptr, portal_detector_->https_request_);
  }

  void StartAttempt() {
    EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
    StartPortalRequest();
    StartTrialTask();
  }

  void ExpectHttpRequestSuccessWithStatus(int status_code) {
    EXPECT_CALL(*http_probe_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));
    auto response =
        std::make_shared<brillo::http::Response>(http_probe_connection_);
    portal_detector_->HttpRequestSuccessCallback(response);
  }

  void ExpectHttpsRequestSuccessWithStatus(int status_code) {
    EXPECT_CALL(*https_probe_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));
    auto response =
        std::make_shared<brillo::http::Response>(https_probe_connection_);
    portal_detector_->HttpsRequestSuccessCallback(response);
  }

 protected:
  StrictMock<MockEventDispatcher> dispatcher_;
  std::shared_ptr<brillo::http::MockTransport> http_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> http_probe_connection_;
  std::shared_ptr<brillo::http::MockTransport> https_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> https_probe_connection_;
  MockHttpRequest* http_request_;
  MockHttpRequest* https_request_;
  CallbackTarget callback_target_;
  const std::string interface_name_;
  std::vector<net_base::IPAddress> dns_servers_;
  std::unique_ptr<PortalDetector> portal_detector_;
};

// static
const int PortalDetectorTest::kNumAttempts = 0;

TEST_F(PortalDetectorTest, Constructor) {
  ExpectReset();
}

TEST_F(PortalDetectorTest, IsInProgress) {
  // Before the trial is started, should not be active.
  EXPECT_FALSE(portal_detector()->IsInProgress());

  // Once the trial is started, IsInProgress should return true.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();

  StartTrialTask();
  EXPECT_TRUE(portal_detector()->IsInProgress());

  // Finish the trial, IsInProgress should return false.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  portal_detector()->CompleteTrial(result);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, HttpStartAttemptFailed) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();

  // Expect that the HTTP request will be started -- return failure.
  EXPECT_CALL(*http_request(), Start)
      .WillOnce(Return(HttpRequest::Error::kDNSFailure));
  EXPECT_CALL(*https_request(), Start).Times(0);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay())).Times(0);

  // Expect a non-final failure to be relayed to the caller.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));

  portal_detector()->StartTrialTask();
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, HttpsStartAttemptFailed) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();

  // Expect that the HTTP request will be started successfully and the
  // HTTPS request will fail to start.
  EXPECT_CALL(*http_request(), Start).WillOnce(Return(std::nullopt));
  EXPECT_CALL(*https_request(), Start)
      .WillOnce(Return(HttpRequest::Error::kDNSFailure));
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay())).Times(0);

  // Expect PortalDetector will wait for HTTP probe completion.
  EXPECT_CALL(callback_target(), ResultCallback).Times(0);

  portal_detector()->StartTrialTask();
  EXPECT_TRUE(portal_detector()->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  Mock::VerifyAndClearExpectations(&callback_target());

  // Finish the trial, IsInProgress should return false.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent;
  result.http_status = PortalDetector::Status::kSuccess;
  result.https_phase = PortalDetector::Phase::kContent,
  result.https_status = PortalDetector::Status::kFailure;
  portal_detector()->CompleteTrial(result);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, FailureToStartDoesNotCauseImmediateRestart) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();

  EXPECT_CALL(*http_request(), Start)
      .WillOnce(Return(HttpRequest::Error::kDNSFailure));
  EXPECT_CALL(*https_request(), Start).Times(0);
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  portal_detector()->StartTrialTask();
  Mock::VerifyAndClearExpectations(&dispatcher_);

  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_positive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
  StartPortalRequest();

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, GetNextAttemptDelayUnchangedUntilTrialStarts) {
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());

  StartTrialTask();
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_positive());
}

TEST_F(PortalDetectorTest, ResetAttemptDelays) {
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();
  StartTrialTask();
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->CompleteTrial({});
  ExpectCleanupTrial();

  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_positive());
  portal_detector_->ResetAttemptDelays();
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();
  StartTrialTask();
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_positive());
  Mock::VerifyAndClearExpectations(&dispatcher_);
}

TEST_F(PortalDetectorTest, Restart) {
  EXPECT_FALSE(portal_detector()->IsInProgress());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_.ToString(), kHttpUrl);
  StartTrialTask();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->CompleteTrial({});
  ExpectCleanupTrial();

  auto next_delay = portal_detector()->GetNextAttemptDelay();
  EXPECT_TRUE(next_delay.is_positive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartAfterRedirect) {
  auto probe_url =
      *net_base::HttpUrl::CreateFromString("http://service.google.com");

  EXPECT_FALSE(portal_detector()->IsInProgress());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.redirect_url =
      net_base::HttpUrl::CreateFromString("https://www.portal.com/login");
  result.probe_url = probe_url;
  result.http_probe_completed = true;
  result.https_probe_completed = true;
  portal_detector()->CompleteTrial(result);
  ExpectCleanupTrial();

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(portal_detector()->http_url_, probe_url);
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, ResetAttemptDelaysAndRestart) {
  EXPECT_FALSE(portal_detector()->IsInProgress());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(portal_detector()->http_url_.ToString(), kHttpUrl);
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->CompleteTrial({});
  ExpectCleanupTrial();

  auto next_delay = portal_detector()->GetNextAttemptDelay();
  EXPECT_TRUE(next_delay.is_positive());

  portal_detector()->ResetAttemptDelays();
  auto reset_delay = portal_detector()->GetNextAttemptDelay();
  EXPECT_TRUE(reset_delay.is_zero());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, MultipleRestarts) {
  EXPECT_FALSE(portal_detector()->IsInProgress());
  EXPECT_FALSE(portal_detector()->IsTrialScheduled());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_TRUE(portal_detector()->GetNextAttemptDelay().is_zero());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->CompleteTrial({});
  ExpectCleanupTrial();

  auto next_delay = portal_detector()->GetNextAttemptDelay();
  EXPECT_TRUE(next_delay.is_positive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
  StartPortalRequest();
  Mock::VerifyAndClearExpectations(&dispatcher_);

  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_FALSE(portal_detector()->IsInProgress());
  EXPECT_TRUE(portal_detector()->IsTrialScheduled());

  EXPECT_TRUE(next_delay.is_positive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
  StartPortalRequest();
  Mock::VerifyAndClearExpectations(&dispatcher_);

  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_FALSE(portal_detector()->IsInProgress());
  EXPECT_TRUE(portal_detector()->IsTrialScheduled());

  StartTrialTask();
  EXPECT_EQ(2, portal_detector()->attempt_count());
  EXPECT_TRUE(portal_detector()->IsInProgress());
  EXPECT_FALSE(portal_detector()->IsTrialScheduled());

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartWhileAlreadyInProgress) {
  EXPECT_FALSE(portal_detector()->IsInProgress());

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  EXPECT_EQ(portal_detector()->GetNextAttemptDelay(), base::TimeDelta());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  StartTrialTask();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_TRUE(portal_detector()->IsInProgress());
  EXPECT_FALSE(portal_detector()->IsTrialScheduled());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  EXPECT_CALL(dispatcher(), PostDelayedTask).Times(0);
  StartPortalRequest();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_TRUE(portal_detector()->IsInProgress());
  EXPECT_FALSE(portal_detector()->IsTrialScheduled());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, AttemptCount) {
  EXPECT_FALSE(portal_detector()->IsInProgress());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_.ToString(), kHttpUrl);
  Mock::VerifyAndClearExpectations(&dispatcher_);

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(3);

  std::set<std::string> expected_retry_http_urls(kFallbackHttpUrls.begin(),
                                                 kFallbackHttpUrls.end());
  expected_retry_http_urls.insert(kHttpUrl);

  std::set<std::string> expected_retry_https_urls(kFallbackHttpsUrls.begin(),
                                                  kFallbackHttpsUrls.end());
  expected_retry_https_urls.insert(kHttpsUrl);

  auto last_delay = base::TimeDelta();
  for (int i = 1; i < 4; i++) {
    if (i == 1) {
      EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, ZeroDelay()));
    } else {
      EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, PositiveDelay()));
    }
    StartPortalRequest();
    StartTrialTask();
    EXPECT_EQ(i, portal_detector()->attempt_count());
    const auto next_delay = portal_detector()->GetNextAttemptDelay();
    EXPECT_GT(next_delay, last_delay);
    last_delay = next_delay;

    EXPECT_NE(
        expected_retry_http_urls.find(portal_detector()->http_url_.ToString()),
        expected_retry_http_urls.end());
    EXPECT_NE(expected_retry_https_urls.find(
                  portal_detector()->https_url_.ToString()),
              expected_retry_https_urls.end());

    portal_detector()->CompleteTrial(result);
    Mock::VerifyAndClearExpectations(&dispatcher_);
  }
  portal_detector()->Stop();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RequestSuccess) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kSuccess;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  ExpectHttpsRequestSuccessWithStatus(204);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  ExpectHttpRequestSuccessWithStatus(204);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  ExpectHttpRequestSuccessWithStatus(123);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  ExpectHttpsRequestSuccessWithStatus(204);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestFail) {
  StartAttempt();

  // HTTPS probe does not trigger anything (for now)
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  ExpectHttpsRequestSuccessWithStatus(123);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  ExpectHttpRequestSuccessWithStatus(123);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestRedirect) {
  StartAttempt();

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.redirect_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  result.probe_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  ExpectHttpsRequestSuccessWithStatus(123);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(kHttpUrl));
  ExpectHttpRequestSuccessWithStatus(302);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestTempRedirect) {
  StartAttempt();

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.redirect_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  result.probe_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsInProgress());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  ExpectHttpsRequestSuccessWithStatus(123);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(kHttpUrl));
  ExpectHttpRequestSuccessWithStatus(307);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestRedirectWithHTTPSProbeTimeout) {
  StartAttempt();
  EXPECT_TRUE(portal_detector_->IsInProgress());

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.https_phase = PortalDetector::Phase::kUnknown;
  result.https_status = PortalDetector::Status::kFailure;
  result.redirect_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  result.probe_url = net_base::HttpUrl::CreateFromString(kHttpUrl);
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(kHttpUrl));
  ExpectHttpRequestSuccessWithStatus(302);
  // The HTTPS probe does not complete.
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, PhaseToString) {
  struct {
    PortalDetector::Phase phase;
    std::string expected_name;
  } test_cases[] = {
      {PortalDetector::Phase::kConnection, "Connection"},
      {PortalDetector::Phase::kDNS, "DNS"},
      {PortalDetector::Phase::kHTTP, "HTTP"},
      {PortalDetector::Phase::kContent, "Content"},
      {PortalDetector::Phase::kUnknown, "Unknown"},
  };

  for (const auto& t : test_cases) {
    EXPECT_EQ(t.expected_name, PortalDetector::PhaseToString(t.phase));
  }
}

TEST_F(PortalDetectorTest, StatusToString) {
  struct {
    PortalDetector::Status status;
    std::string expected_name;
  } test_cases[] = {
      {PortalDetector::Status::kSuccess, "Success"},
      {PortalDetector::Status::kTimeout, "Timeout"},
      {PortalDetector::Status::kRedirect, "Redirect"},
      {PortalDetector::Status::kFailure, "Failure"},
  };

  for (const auto& t : test_cases) {
    EXPECT_EQ(t.expected_name, PortalDetector::StatusToString(t.status));
  }
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

  // The loop index starts at 1 to force a non-zero |attempt_count_| and to
  // force using the fallback list.
  for (int i = 1; i < 100; i++) {
    portal_detector_->attempt_count_ = i;
    EXPECT_EQ(portal_detector_->PickProbeUrl(url1, {}), url1);

    const auto& found =
        portal_detector_->PickProbeUrl(url1, {url2, url3}).ToString();
    if (i == 1) {
      EXPECT_EQ(url2.ToString(), found);
    } else if (i == 2) {
      EXPECT_EQ(url3.ToString(), found);
    } else {
      all_found_urls.insert(found);
    }
    EXPECT_NE(all_urls.find(found), all_urls.end());
  }
  // Probability this assert fails = 3 * 1/3 ^ 97 + 3 * 2/3 ^ 97
  EXPECT_EQ(all_urls, all_found_urls);
}

}  // namespace shill
