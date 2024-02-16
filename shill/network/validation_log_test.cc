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

#include "shill/metrics.h"
#include "shill/mock_metrics.h"
#include "shill/network/capport_proxy.h"
#include "shill/portal_detector.h"
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
  ~ValidationLogTest() override = default;

  CapportStatus GetCAPPORTCaptiveStatus() {
    CapportStatus s;
    s.is_captive = true;
    s.user_portal_url =
        net_base::HttpUrl::CreateFromString("https://portal.com/login");
    return s;
  }

  CapportStatus GetCAPPORTCaptiveNoPortalURLStatus() {
    CapportStatus s;
    s.is_captive = true;
    s.user_portal_url = std::nullopt;
    return s;
  }

  CapportStatus GetCAPPORTNotCaptiveStatus() {
    CapportStatus s;
    s.is_captive = false;
    s.user_portal_url =
        net_base::HttpUrl::CreateFromString("https://portal.com/login");
    return s;
  }

  PortalDetector::Result GetInternetConnectivityResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kSuccess;
    r.http_status_code = 204;
    r.http_content_length = 0;
    r.https_result = PortalDetector::ProbeResult::kSuccess;
    CHECK_EQ(PortalDetector::ValidationState::kInternetConnectivity,
             r.GetValidationState());
    return r;
  }

  PortalDetector::Result GetPortalRedirectResult() {
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

  PortalDetector::Result GetPortalSuspectedResult() {
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

  PortalDetector::Result GetNoConnectivityResult() {
    PortalDetector::Result r;
    r.http_result = PortalDetector::ProbeResult::kConnectionFailure;
    r.https_result = PortalDetector::ProbeResult::kConnectionFailure;
    CHECK_EQ(PortalDetector::ValidationState::kNoConnectivity,
             r.GetValidationState());
    return r;
  }

  void AddPortalDetectorResult(const PortalDetector::Result& r) {
    // Ensure that all durations between events are positive.
    dispatcher_.task_environment().FastForwardBy(base::Milliseconds(10));
    log_.AddPortalDetectorResult(r);
  }

  void AddCAPPORTStatus(const CapportStatus& s) {
    // Ensure that all durations between events are positive.
    dispatcher_.task_environment().FastForwardBy(base::Milliseconds(10));
    log_.AddCAPPORTStatus(s);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  StrictMock<MockMetrics> metrics_;
  ValidationLog log_;
};

TEST_F(ValidationLogTest, InternetConnectivityDirectly) {
  AddPortalDetectorResult(GetInternetConnectivityResult());

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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, IgnoreResultAfterInternetConnectivity) {
  AddPortalDetectorResult(GetInternetConnectivityResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetNoConnectivityResult());

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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, PortalRedirectResult) {
  AddPortalDetectorResult(GetPortalRedirectResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, PortalSuspectedResult) {
  AddPortalDetectorResult(GetPortalSuspectedResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, NoConnectivityResult) {
  AddPortalDetectorResult(GetNoConnectivityResult());

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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, MultipleNoConnectivityResults) {
  AddPortalDetectorResult(GetNoConnectivityResult());
  AddPortalDetectorResult(GetNoConnectivityResult());
  AddPortalDetectorResult(GetNoConnectivityResult());

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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, NoConnectivityThenPortalSuspectedResults) {
  AddPortalDetectorResult(GetNoConnectivityResult());
  AddPortalDetectorResult(GetNoConnectivityResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetNoConnectivityResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, MultiplePortalRedirectAndSuspectedResults) {
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, InternetConnectivityAfterPortalRedirects) {
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetInternetConnectivityResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest,
       InternetConnectivityAfterPortalRedirectsAndSuspectedResult) {
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetInternetConnectivityResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, InternetConnectivityAfterPortalSuspectedResult) {
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetNoConnectivityResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetInternetConnectivityResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, IgnoreRedirectResultsAfterInternetConnectivity) {
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetPortalSuspectedResult());
  AddPortalDetectorResult(GetInternetConnectivityResult());
  AddPortalDetectorResult(GetInternetConnectivityResult());
  AddPortalDetectorResult(GetPortalRedirectResult());
  AddPortalDetectorResult(GetPortalRedirectResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, CAPPORTOpensDirectly) {
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult, _, _))
      .Times(0);

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, CAPPORTRemainsCaptive) {
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult,
                            Technology::kWiFi,
                            Metrics::kAggregateCAPPORTResultCaptive));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, CAPPORTOpensWithoutUserPortalURL) {
  AddCAPPORTStatus(GetCAPPORTCaptiveNoPortalURLStatus());
  AddPortalDetectorResult(GetInternetConnectivityResult());
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());

  // Probe metrics expectations
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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  // CAPPORT metrics expectations
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive,
                        Technology::kWiFi, _));
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult,
                            Technology::kWiFi,
                            Metrics::kAggregateCAPPORTResultOpenWithInternet));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, CAPPORTOpensWithoutInternetAccess) {
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL,
                        Technology::kWiFi, _));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult,
                    Technology::kWiFi,
                    Metrics::kAggregateCAPPORTResultOpenWithoutInternet));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, CAPPORTOpensWithUserPortalURL) {
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTCaptiveStatus());
  AddPortalDetectorResult(GetInternetConnectivityResult());
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());
  AddCAPPORTStatus(GetCAPPORTNotCaptiveStatus());

  // Probe metrics expectations
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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL));

  // CAPPORT metrics expectations
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL,
                        Technology::kWiFi, _));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult,
                            Technology::kWiFi,
                            Metrics::kAggregateCAPPORTResultOpenWithInternet));

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, ValidationLogRecordMetricsWithoutRecord) {
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportSupported, _, _))
      .Times(0);
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportAdvertised, _, _))
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
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult, _, _))
      .Times(0);

  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, ValidationLogRecordMetricsCapportSupported) {
  PortalDetector::Result redirect_result;
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

  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricCapportAdvertised,
                                      Technology::kWiFi,
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
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalNoURL));

  AddPortalDetectorResult(redirect_result);
  log_.SetCapportDHCPSupported();
  log_.RecordMetrics();
}

TEST_F(ValidationLogTest, ValidationLogRecordMetricTermsAndConditionsURL) {
  AddPortalDetectorResult(GetInternetConnectivityResult());

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

  EXPECT_CALL(metrics_,
              SendEnumToUMA(
                  Metrics::kMetricTermsAndConditionsAggregateResult,
                  Metrics::kTermsAndConditionsAggregateResultNoPortalWithURL));

  log_.SetHasTermsAndConditions();
  log_.RecordMetrics();
}

TEST_F(ValidationLogTest,
       ValidationLogRecordMetricTermsAndConditionsURLWithRedirect) {
  AddPortalDetectorResult(GetPortalRedirectResult());

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
  EXPECT_CALL(metrics_,
              SendEnumToUMA(Metrics::kMetricCapportSupported, Technology::kWiFi,
                            Metrics::kCapportNotSupported));
  EXPECT_CALL(
      metrics_,
      SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                    Metrics::kTermsAndConditionsAggregateResultPortalWithURL));

  log_.SetHasTermsAndConditions();
  log_.RecordMetrics();
}

}  // namespace
}  // namespace shill
