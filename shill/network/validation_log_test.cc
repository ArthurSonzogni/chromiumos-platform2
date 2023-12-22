// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/validation_log.h"

#include <optional>
#include <vector>

#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/http_request.h"
#include "shill/metrics.h"
#include "shill/mock_metrics.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"

namespace shill {
namespace {

using ::testing::_;
using ::testing::Mock;
using ::testing::Ne;
using ::testing::StrictMock;

TEST(ValidationLogTest, ValidationLogRecordMetrics) {
  EventDispatcherForTest dispatcher;

  // Stub PortalDetector results:
  NetworkMonitor::Result i, r, p, n;

  // |i| -> kInternetConnectivity
  i.http_result = PortalDetector::HTTPProbeResult::kSuccess;
  i.http_status_code = 204;
  i.http_content_length = 0;
  i.http_probe_completed = true;
  i.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            i.GetValidationState());

  // |r| -> kPortalRedirect
  r.http_result = PortalDetector::HTTPProbeResult::kPortalRedirect;
  r.http_status_code = 302;
  r.http_content_length = 0;
  r.https_error = HttpRequest::Error::kConnectionFailure;
  r.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  r.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  r.http_probe_completed = true;
  r.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            r.GetValidationState());

  // |p| -> kPortalSuspected
  p.http_result = PortalDetector::HTTPProbeResult::kPortalSuspected;
  p.http_status_code = 200;
  p.http_content_length = 678;
  p.https_error = HttpRequest::Error::kConnectionFailure;
  p.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  p.http_probe_completed = true;
  p.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            p.GetValidationState());

  // |n| -> kNoConnectivity
  n.http_result = PortalDetector::HTTPProbeResult::kConnectionFailure;
  n.https_error = HttpRequest::Error::kConnectionFailure;
  n.http_probe_completed = true;
  n.https_probe_completed = true;
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            n.GetValidationState());

  struct {
    std::vector<NetworkMonitor::Result> results;
    Metrics::PortalDetectorAggregateResult expected_metric_enum;
  } test_cases[] = {
      {{i}, Metrics::kPortalDetectorAggregateResultInternet},
      {{i, r, p, n}, Metrics::kPortalDetectorAggregateResultInternet},
      {{r}, Metrics::kPortalDetectorAggregateResultRedirect},
      {{p}, Metrics::kPortalDetectorAggregateResultPartialConnectivity},
      {{n}, Metrics::kPortalDetectorAggregateResultNoConnectivity},
      {{n, n, n}, Metrics::kPortalDetectorAggregateResultNoConnectivity},
      {{n, n, p, n},
       Metrics::kPortalDetectorAggregateResultPartialConnectivity},
      {{p, r, r, p}, Metrics::kPortalDetectorAggregateResultRedirect},
      {{r, r, r, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterRedirect},
      {{r, p, r, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterRedirect},
      {{p, n, p, i},
       Metrics::kPortalDetectorAggregateResultInternetAfterPartialConnectivity},
      {{p, p, i, i, r, r},
       Metrics::kPortalDetectorAggregateResultInternetAfterPartialConnectivity},
  };

  for (const auto& tt : test_cases) {
    StrictMock<MockMetrics> metrics;
    ValidationLog log(Technology::kWiFi, &metrics);
    for (auto r : tt.results) {
      // Ensure that all durations between events are positive.
      dispatcher.task_environment().FastForwardBy(base::Milliseconds(10));
      log.AddResult(r);
    }

    EXPECT_CALL(metrics,
                SendEnumToUMA(Metrics::kPortalDetectorAggregateResult,
                              Technology::kWiFi, tt.expected_metric_enum));
    switch (tt.expected_metric_enum) {
      case Metrics::kPortalDetectorAggregateResultInternet:
      case Metrics::
          kPortalDetectorAggregateResultInternetAfterPartialConnectivity:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToInternet,
                                       Technology::kWiFi, _));
        break;
      case Metrics::kPortalDetectorAggregateResultRedirect:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                       Technology::kWiFi, _));
        EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                           Metrics::kCapportNotSupported));
        break;
      case Metrics::kPortalDetectorAggregateResultInternetAfterRedirect:
        EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                                       Technology::kWiFi, _));
        EXPECT_CALL(
            metrics,
            SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect,
                      Technology::kWiFi, _));
        EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                           Metrics::kCapportNotSupported));
        break;
      case Metrics::kPortalDetectorAggregateResultNoConnectivity:
      case Metrics::kPortalDetectorAggregateResultPartialConnectivity:
      case Metrics::kPortalDetectorAggregateResultUnknown:
      default:
        EXPECT_CALL(metrics,
                    SendToUMA(Metrics::kPortalDetectorTimeToInternet, _, _))
            .Times(0);
        EXPECT_CALL(metrics,
                    SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
            .Times(0);
        EXPECT_CALL(
            metrics,
            SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect, _,
                      _))
            .Times(0);
        break;
    }

    log.RecordMetrics();
    Mock::VerifyAndClearExpectations(&metrics);
  }
}

TEST(ValidationLogTest, ValidationLogRecordMetricsWithoutRecord) {
  StrictMock<MockMetrics> metrics;

  EXPECT_CALL(metrics,
              SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, _, _))
      .Times(0);
  EXPECT_CALL(metrics, SendToUMA(Metrics::kPortalDetectorTimeToRedirect, _, _))
      .Times(0);
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported, _))
      .Times(0);
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportAdvertised, _))
      .Times(0);

  ValidationLog log(Technology::kWiFi, &metrics);
  log.RecordMetrics();
}

TEST(ValidationLogTest, ValidationLogRecordMetricsCapportSupported) {
  NetworkMonitor::Result redirect_result;
  redirect_result.http_result =
      PortalDetector::HTTPProbeResult::kPortalRedirect;
  redirect_result.http_status_code = 302;
  redirect_result.http_content_length = 0;
  redirect_result.https_error = HttpRequest::Error::kConnectionFailure;
  redirect_result.redirect_url =
      net_base::HttpUrl::CreateFromString("https://portal.com/login");
  redirect_result.probe_url = net_base::HttpUrl::CreateFromString(
      "https://service.google.com/generate_204");
  redirect_result.http_probe_completed = true;
  redirect_result.https_probe_completed = true;

  MockMetrics metrics;
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                     Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportSupported,
                                     Ne(Metrics::kCapportSupportedByDHCPv4)))
      .Times(0);
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportAdvertised,
                                     Metrics::kCapportSupportedByDHCPv4));
  EXPECT_CALL(metrics, SendEnumToUMA(Metrics::kMetricCapportAdvertised,
                                     Ne(Metrics::kCapportSupportedByDHCPv4)))
      .Times(0);

  ValidationLog log(Technology::kWiFi, &metrics);
  log.AddResult(redirect_result);
  log.SetCapportDHCPSupported();
  log.RecordMetrics();
}

}  // namespace
}  // namespace shill
