// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/net/mock_time.h"

using base::Bind;
using base::Callback;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kBadURL[] = "badurl";
const char kLoggingTag[] = "int0 IPv4 attempt=1 HTTP probe";
const char kInterfaceName[] = "int0";
const char kHttpUrl[] = "http://www.chromium.org";
const char kHttpsUrl[] = "https://www.google.com";
const vector<string> kFallbackHttpUrls{
    "http://www.google.com/gen_204",
    "http://play.googleapis.com/generate_204",
};
const IPAddress kIpAddress = IPAddress("1.2.3.4");
const char kDNSServer0[] = "8.8.8.8";
const char kDNSServer1[] = "8.8.4.4";
const char* const kDNSServers[] = {kDNSServer0, kDNSServer1};

class MockHttpRequest : public HttpRequest {
 public:
  MockHttpRequest()
      : HttpRequest(nullptr,
                    kLoggingTag,
                    kInterfaceName,
                    IPAddress(IPAddress::kFamilyIPv4),
                    {},
                    true){};
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;
  ~MockHttpRequest() = default;

  MOCK_METHOD(
      HttpRequest::Result,
      Start,
      (const std::string&,
       const brillo::http::HeaderList&,
       const base::Callback<void(std::shared_ptr<brillo::http::Response>)>&,
       const base::Callback<void(Result)>&),
      (override));
  MOCK_METHOD(void, Stop, (), (override));
};

}  // namespace

MATCHER_P(IsResult, result, "") {
  return (result.http_phase == arg.http_phase &&
          result.http_status == arg.http_status &&
          result.https_phase == arg.https_phase &&
          result.https_status == arg.https_status &&
          result.redirect_url_string == arg.redirect_url_string &&
          result.probe_url_string == arg.probe_url_string);
}

class PortalDetectorTest : public Test {
 public:
  PortalDetectorTest()
      : manager_(&control_, &dispatcher_, nullptr),
        device_info_(new NiceMock<MockDeviceInfo>(&manager_)),
        connection_(new StrictMock<MockConnection>(device_info_.get())),
        transport_(std::make_shared<brillo::http::MockTransport>()),
        brillo_connection_(
            std::make_shared<brillo::http::MockConnection>(transport_)),
        portal_detector_(
            new PortalDetector(connection_.get(),
                               &dispatcher_,
                               &metrics_,
                               callback_target_.result_callback())),
        interface_name_(kInterfaceName),
        dns_servers_(kDNSServers, kDNSServers + 2),
        http_request_(nullptr),
        https_request_(nullptr) {
    current_time_.tv_sec = current_time_.tv_usec = 0;
  }

  void SetUp() override {
    EXPECT_CALL(*connection_, local()).WillRepeatedly(ReturnRef(kIpAddress));
    EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
    EXPECT_CALL(*connection_, interface_name())
        .WillRepeatedly(ReturnRef(interface_name_));
    portal_detector_->time_ = &time_;
    EXPECT_CALL(time_, GetTimeMonotonic(_))
        .WillRepeatedly(Invoke(this, &PortalDetectorTest::GetTimeMonotonic));
    EXPECT_CALL(*connection_, dns_servers())
        .WillRepeatedly(ReturnRef(dns_servers_));
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
  }

  void TearDown() override {
    Mock::VerifyAndClearExpectations(&http_request_);
    if (portal_detector()->http_request_) {
      EXPECT_CALL(*http_request(), Stop());
      EXPECT_CALL(*https_request(), Stop());

      // Delete the portal detector while expectations still exist.
      portal_detector_.reset();
    }
    testing::Mock::VerifyAndClearExpectations(brillo_connection_.get());
    brillo_connection_.reset();
    testing::Mock::VerifyAndClearExpectations(transport_.get());
    transport_.reset();
  }

 protected:
  static const int kNumAttempts;

  class CallbackTarget {
   public:
    CallbackTarget()
        : result_callback_(
              Bind(&CallbackTarget::ResultCallback, Unretained(this))) {}

    MOCK_METHOD(void, ResultCallback, (const PortalDetector::Result&));

    Callback<void(const PortalDetector::Result&)>& result_callback() {
      return result_callback_;
    }

   private:
    Callback<void(const PortalDetector::Result&)> result_callback_;
  };

  void AssignHttpRequest() {
    http_request_ = new StrictMock<MockHttpRequest>();
    https_request_ = new StrictMock<MockHttpRequest>();
    portal_detector_->http_request_.reset(http_request_);
    portal_detector_->https_request_.reset(
        https_request_);  // Passes ownership.
  }

  bool StartPortalRequest(const PortalDetector::Properties& props, int delay) {
    bool ret = portal_detector_->StartAfterDelay(props, delay);
    if (ret) {
      AssignHttpRequest();
    }
    return ret;
  }

  void StartTrialTask() {
    EXPECT_CALL(*http_request(), Start(_, _, _, _))
        .WillOnce(Return(HttpRequest::kResultInProgress));
    EXPECT_CALL(*https_request(), Start(_, _, _, _))
        .WillOnce(Return(HttpRequest::kResultInProgress));
    portal_detector()->StartTrialTask();
  }

  MockHttpRequest* http_request() { return http_request_; }
  MockHttpRequest* https_request() { return https_request_; }
  PortalDetector* portal_detector() { return portal_detector_.get(); }
  MockEventDispatcher& dispatcher() { return dispatcher_; }
  CallbackTarget& callback_target() { return callback_target_; }
  MockMetrics& metrics() { return metrics_; }
  brillo::http::MockConnection* brillo_connection() {
    return brillo_connection_.get();
  }

  void ExpectReset() {
    EXPECT_FALSE(portal_detector_->attempt_count_);
    EXPECT_TRUE(callback_target_.result_callback() ==
                portal_detector_->portal_result_callback_);
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
    EXPECT_EQ(nullptr, portal_detector_->https_request_);
  }

  void AdvanceTime(int milliseconds) {
    struct timeval tv = {milliseconds / 1000, (milliseconds % 1000) * 1000};
    timeradd(&current_time_, &tv, &current_time_);
  }

  void StartAttempt() {
    EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
    PortalDetector::Properties props =
        PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
    EXPECT_TRUE(StartPortalRequest(props, 0));
    StartTrialTask();
  }

  void ExpectRequestSuccessWithStatus(int status_code, bool is_http) {
    EXPECT_CALL(*brillo_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));

    auto response =
        std::make_shared<brillo::http::Response>(brillo_connection_);
    if (is_http)
      portal_detector_->HttpRequestSuccessCallback(response);
    else
      portal_detector_->HttpsRequestSuccessCallback(response);
  }

 private:
  int GetTimeMonotonic(struct timeval* tv) {
    *tv = current_time_;
    return 0;
  }

  StrictMock<MockEventDispatcher> dispatcher_;
  MockControl control_;
  MockManager manager_;
  std::unique_ptr<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  std::shared_ptr<brillo::http::MockTransport> transport_;
  NiceMock<MockMetrics> metrics_;
  std::shared_ptr<brillo::http::MockConnection> brillo_connection_;
  CallbackTarget callback_target_;
  std::unique_ptr<PortalDetector> portal_detector_;
  StrictMock<MockTime> time_;
  struct timeval current_time_;
  const string interface_name_;
  vector<string> dns_servers_;
  MockHttpRequest* http_request_;
  MockHttpRequest* https_request_;
};

// static
const int PortalDetectorTest::kNumAttempts = 0;

TEST_F(PortalDetectorTest, Constructor) {
  ExpectReset();
}

TEST_F(PortalDetectorTest, InvalidURL) {
  EXPECT_FALSE(portal_detector()->IsActive());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(0);
  PortalDetector::Properties props =
      PortalDetector::Properties(kBadURL, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_FALSE(StartPortalRequest(props, 0));
  ExpectReset();

  EXPECT_FALSE(portal_detector()->IsActive());
}

TEST_F(PortalDetectorTest, IsActive) {
  // Before the trial is started, should not be active.
  EXPECT_FALSE(portal_detector()->IsActive());

  // Once the trial is started, IsActive should return true.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  StartTrialTask();
  EXPECT_TRUE(portal_detector()->IsActive());

  // Finish the trial, IsActive should return false.
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kSuccess;
  portal_detector()->CompleteTrial(result);
  EXPECT_FALSE(portal_detector()->IsActive());
}

TEST_F(PortalDetectorTest, StartAttemptFailed) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  // Expect that the request will be started -- return failure.
  EXPECT_CALL(*http_request(), Start(_, _, _, _))
      .WillOnce(Return(HttpRequest::kResultDNSFailure));

  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);

  // Expect a non-final failure to be relayed to the caller.
  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));

  portal_detector()->StartTrialTask();
}

TEST_F(PortalDetectorTest, AdjustStartDelayImmediate) {
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  EXPECT_TRUE(StartPortalRequest(props, 0));

  EXPECT_EQ(portal_detector()->AdjustStartDelay(1), 1);
}

TEST_F(PortalDetectorTest, AdjustStartDelayAfterDelay) {
  const int kDelaySeconds = 123;
  // The first attempt should be delayed by kDelaySeconds.
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, kDelaySeconds * 1000));

  EXPECT_TRUE(StartPortalRequest(props, kDelaySeconds));

  AdvanceTime(kDelaySeconds * 1000);

  EXPECT_EQ(portal_detector()->AdjustStartDelay(1), 1);
}

TEST_F(PortalDetectorTest, StartRepeated) {
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0)).Times(1);
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));

  // A second  should cancel the existing trial and set up the new one.
  EXPECT_CALL(*http_request(), Stop());
  EXPECT_CALL(*https_request(), Stop());
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 10 * 1000)).Times(1);
  EXPECT_TRUE(portal_detector()->StartAfterDelay(props, 10));
}

TEST_F(PortalDetectorTest, AttemptCount) {
  EXPECT_FALSE(portal_detector()->IsInProgress());
  // Expect the PortalDetector to immediately post a task for the each attempt.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, _)).Times(4);
  PortalDetector::Properties props =
      PortalDetector::Properties(kHttpUrl, kHttpsUrl, kFallbackHttpUrls);
  EXPECT_TRUE(StartPortalRequest(props, 0));
  EXPECT_EQ(portal_detector()->http_url_string_, kHttpUrl);

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kDNS,
  result.http_status = PortalDetector::Status::kFailure;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.num_attempts = kNumAttempts;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(3);

  // Expect the PortalDetector to stop the trial after
  // the final attempt.
  EXPECT_CALL(*http_request(), Stop()).Times(7);
  EXPECT_CALL(*https_request(), Stop()).Times(7);

  int init_delay = 3;
  for (int i = 0; i < 3; i++) {
    int delay = portal_detector()->AdjustStartDelay(init_delay);
    EXPECT_EQ(delay, init_delay);
    portal_detector()->StartAfterDelay(props, delay);
    EXPECT_NE(portal_detector()->http_url_string_, kHttpUrl);
    AdvanceTime(delay * 1000);
    portal_detector()->CompleteTrial(result);
    init_delay *= 2;
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
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(204, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_));
  ExpectRequestSuccessWithStatus(204, true);
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
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, true);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_));
  ExpectRequestSuccessWithStatus(204, false);
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
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_));
  ExpectRequestSuccessWithStatus(123, true);
}

TEST_F(PortalDetectorTest, RequestRedirect) {
  StartAttempt();

  PortalDetector::Result result;
  result.http_phase = PortalDetector::Phase::kContent,
  result.http_status = PortalDetector::Status::kRedirect;
  result.https_phase = PortalDetector::Phase::kContent;
  result.https_status = PortalDetector::Status::kFailure;
  result.redirect_url_string = kHttpUrl;
  result.probe_url_string = kHttpUrl;
  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result))).Times(0);
  EXPECT_CALL(*http_request(), Stop()).Times(0);
  EXPECT_CALL(*https_request(), Stop()).Times(0);
  ExpectRequestSuccessWithStatus(123, false);

  EXPECT_CALL(callback_target(), ResultCallback(IsResult(result)));
  EXPECT_CALL(*http_request(), Stop()).Times(1);
  EXPECT_CALL(*https_request(), Stop()).Times(1);
  EXPECT_CALL(*brillo_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(kHttpUrl));
  EXPECT_CALL(metrics(), NotifyPortalDetectionMultiProbeResult(_));
  ExpectRequestSuccessWithStatus(302, true);
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

}  // namespace shill
