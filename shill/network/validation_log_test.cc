// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/validation_log.h"

#include <optional>
#include <vector>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/http_request.h"
#include "shill/metrics.h"
#include "shill/mock_metrics.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"

#include <base/logging.h>
namespace shill {
namespace {

using ::testing::_;
using ::testing::Mock;
using ::testing::Ne;
using ::testing::StrictMock;

class ValidationLogTest : public testing::Test {
 public:
  ValidationLogTest() : log_(Technology::kWiFi, &metrics_) {}
  ~ValidationLogTest() override {}

  NetworkMonitor::Result GetInternetConnectivityResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kSuccess;
    r.http_status_code = 204;
    r.http_content_length = 0;
    r.https_result = PortalDetector::ProbeResult::kSuccess;
    CHECK_EQ(PortalDetector::ValidationState::kInternetConnectivity,
             r.GetValidationState());
    return r;
  }

  NetworkMonitor::Result GetPortalRedirectResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kPortalRedirect;
    r.http_status_code = 302;
    r.http_content_length = 0;
    r.https_result = PortalDetector::ProbeResult::kConnectionFailure;
    r.redirect_url =
        net_base::HttpUrl::CreateFromString("https://portal.com/login");
    r.probe_url = net_base::HttpUrl::CreateFromString(
        "https://service.google.com/generate_204");
    CHECK_EQ(PortalDetector::ValidationState::kPortalRedirect,
             r.GetValidationState());
    return r;
  }

  NetworkMonitor::Result GetPortalSuspectedResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kPortalSuspected;
    r.http_status_code = 200;
    r.http_content_length = 678;
    r.https_result = PortalDetector::ProbeResult::kConnectionFailure;
    r.probe_url = net_base::HttpUrl::CreateFromString(
        "https://service.google.com/generate_204");
    CHECK_EQ(PortalDetector::ValidationState::kPortalSuspected,
             r.GetValidationState());
    return r;
  }

  NetworkMonitor::Result GetNoConnectivityResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kConnectionFailure;
    r.https_result = PortalDetector::ProbeResult::kConnectionFailure;
    CHECK_EQ(PortalDetector::ValidationState::kNoConnectivity,
             r.GetValidationState());
    return r;
  }

  void AddResult(const NetworkMonitor::Result& r) {
    // Ensure that all durations between events are positive.
    dispatcher_.task_environment().FastForwardBy(base::Milliseconds(10));
    log_.AddResult(r);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  StrictMock<MockMetrics> metrics_;
  ValidationLog log_;
};

TEST_F(ValidationLogTest, InternetConnectivityDirectly) {
  AddResult(GetInternetConnectivityResult());

  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorInitialResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultInternet));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                  Technology::kWiFi, _));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, IgnoreResultAfterInternetConnectivity) {
  AddResult(GetInternetConnectivityResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalSuspectedResult());
  AddResult(GetNoConnectivityResult());

  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorInitialResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultInternet));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                  Technology::kWiFi, _));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, PortalRedirectResult) {
  AddResult(GetPortalRedirectResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultRedirect));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                        Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                  Technology::kWiFi, _));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                      Metrics::kCapportNotSupported));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, PortalSuspectedResult) {
  AddResult(GetPortalSuspectedResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(
                  Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                  Metrics::kPortalDetectorAggregateResultPartialConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 1));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, NoConnectivityResult) {
  AddResult(GetNoConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultNoConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 1));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, MultipleNoConnectivityResults) {
  AddResult(GetNoConnectivityResult());
  AddResult(GetNoConnectivityResult());
  AddResult(GetNoConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure))
      .Times(2);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultNoConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 3));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, NoConnectivityThenPortalSuspectedResults) {
  AddResult(GetNoConnectivityResult());
  AddResult(GetNoConnectivityResult());
  AddResult(GetPortalSuspectedResult());
  AddResult(GetNoConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure))
      .Times(2);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure))
      .Times(1);
  EXPECT_CALL(metrics_,
              SendEnumToUMA(
                  Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                  Metrics::kPortalDetectorAggregateResultPartialConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 4));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, MultiplePortalRedirectAndSuspectedResults) {
  AddResult(GetPortalSuspectedResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalSuspectedResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound))
      .Times(2);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure))
      .Times(1);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultRedirect));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                        Technology::kWiFi, 2));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 4));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                  Technology::kWiFi, _));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                      Metrics::kCapportNotSupported));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, InternetConnectivityAfterPortalRedirects) {
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetInternetConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound))
      .Times(2);
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorRetryResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline))
      .Times(1);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(
          Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
          Metrics::kPortalDetectorAggregateResultInternetAfterRedirect));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 4));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                        Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                  Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                      Metrics::kCapportNotSupported));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest,
       InternetConnectivityAfterPortalRedirectsAndSuspectedResult) {
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalSuspectedResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetInternetConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorRetryResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(
          Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
          Metrics::kPortalDetectorAggregateResultInternetAfterRedirect));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 4));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                        Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                  Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                      Metrics::kCapportNotSupported));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, InternetConnectivityAfterPortalSuspectedResult) {
  AddResult(GetPortalSuspectedResult());
  AddResult(GetNoConnectivityResult());
  AddResult(GetPortalSuspectedResult());
  AddResult(GetInternetConnectivityResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultConnectionFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorRetryResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(
          Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
          Metrics::
              kPortalDetectorAggregateResultInternetAfterPartialConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 4));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                  Technology::kWiFi, _));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, IgnoreRedirectResultsAfterInternetConnectivity) {
  AddResult(GetPortalSuspectedResult());
  AddResult(GetPortalSuspectedResult());
  AddResult(GetInternetConnectivityResult());
  AddResult(GetInternetConnectivityResult());
  AddResult(GetPortalRedirectResult());
  AddResult(GetPortalRedirectResult());

  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorRetryResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultHTTPSFailure));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kPortalDetectorRetryResult,
                                      Technology::kWiFi,
                                      Metrics::kPortalDetectorResultOnline));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(
          Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
          Metrics::
              kPortalDetectorAggregateResultInternetAfterPartialConnectivity));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                                  Technology::kWiFi, 3));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                  Technology::kWiFi, _));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, ValidationLogRecordMetricsWithoutRecord) {
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportAdvertised, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorInitialResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorRetryResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToOnline, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToOnline, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect, _, _))
      .Times(0);

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, ValidationLogRecordMetricsCapportSupported) {
  NetworkMonitor::Result redirect_result;
  redirect_result.http_result = PortalDetector::ProbeResult::kPortalRedirect;
  redirect_result.http_status_code = 302;
  redirect_result.http_content_length = 0;
  redirect_result.https_result =
      PortalDetector::ProbeResult::kConnectionFailure;
  redirect_result.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  redirect_result.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  redirect_result.http_duration = base::Milliseconds(100);
  redirect_result.https_duration = base::Seconds(8);

  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                      Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportAdvertised,
                                      Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                        Technology::kWiFi, 1));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                                  Technology::kWiFi, 1));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorInitialResult, Technology::kWiFi,
                    Metrics::kPortalDetectorResultRedirectFound));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, Technology::kWiFi,
                    Metrics::kPortalDetectorAggregateResultRedirect));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                  Technology::kWiFi, _));

  AddResult(redirect_result);
  log_.SetCapportDHCPSupported();
  log_.RecordMetrics();
}

}  // namespace
}  // namespace shill
