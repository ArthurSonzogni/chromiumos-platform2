// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/validation_log.h"

#include <optional>
#include <vector>

#include "shill/metrics.h"
#include "shill/network/network_monitor.h"
#include "shill/technology.h"

namespace shill {

ValidationLog::ValidationLog(Technology technology, Metrics* metrics)
    : technology_(technology),
      metrics_(metrics),
      connection_start_(base::TimeTicks::Now()) {}

ValidationLog::~ValidationLog() = default;

void ValidationLog::AddResult(const PortalDetector::Result& result) {
  // Make sure that the total memory taken by ValidationLog is bounded.
  static constexpr size_t kValidationLogMaxSize = 128;
  if (results_.size() < kValidationLogMaxSize) {
    results_.emplace_back(base::TimeTicks::Now(), result.GetValidationState(),
                          result.GetResultMetric());
  }
}

void ValidationLog::SetCapportDHCPSupported() {
  capport_dhcp_supported_ = true;
}

void ValidationLog::SetCapportRASupported() {
  capport_ra_supported_ = true;
}

void ValidationLog::RecordMetrics() const {
  if (results_.empty()) {
    return;
  }

  int total_attempts = 0;
  bool has_internet = false;
  bool has_redirect = false;
  bool has_suspected_redirect = false;
  for (const auto& result_data : results_) {
    total_attempts++;
    metrics_->SendEnumToUMA(total_attempts == 1
                                ? Metrics::kPortalDetectorInitialResult
                                : Metrics::kPortalDetectorRetryResult,
                            technology_, result_data.metric_result);
    switch (result_data.validation_state) {
      case PortalDetector::ValidationState::kNoConnectivity:
        break;
      case PortalDetector::ValidationState::kPortalSuspected:
        has_suspected_redirect = true;
        break;
      case PortalDetector::ValidationState::kPortalRedirect:
        if (!has_redirect) {
          has_redirect = true;
          const base::TimeDelta time_to_redirect =
              result_data.timestamp - connection_start_;
          metrics_->SendToUMA(Metrics::kPortalDetectorTimeToRedirect,
                              technology_, time_to_redirect.InMilliseconds());
          metrics_->SendToUMA(Metrics::kPortalDetectorAttemptsToRedirectFound,
                              technology_, total_attempts);
        }
        break;
      case PortalDetector::ValidationState::kInternetConnectivity:
        if (!has_internet) {
          has_internet = true;
          const auto& metric =
              !has_redirect
                  ? Metrics::kPortalDetectorTimeToInternet
                  : Metrics::kPortalDetectorTimeToInternetAfterRedirect;
          const base::TimeDelta time_to_internet =
              result_data.timestamp - connection_start_;
          metrics_->SendToUMA(metric, technology_,
                              time_to_internet.InMilliseconds());
          metrics_->SendToUMA(Metrics::kPortalDetectorAttemptsToOnline,
                              technology_, total_attempts);
        }
        break;
    }
    // Ignores all results after the first kInternetConnectivity result.
    if (has_internet) {
      break;
    }
  }

  if (!has_internet) {
    metrics_->SendToUMA(Metrics::kPortalDetectorAttemptsToDisconnect,
                        technology_, total_attempts);
  }

  Metrics::PortalDetectorAggregateResult netval_result =
      Metrics::kPortalDetectorAggregateResultUnknown;
  if (has_internet && has_redirect) {
    netval_result =
        Metrics::kPortalDetectorAggregateResultInternetAfterRedirect;
  } else if (has_internet && has_suspected_redirect) {
    netval_result =
        Metrics::kPortalDetectorAggregateResultInternetAfterPartialConnectivity;
  } else if (has_internet) {
    netval_result = Metrics::kPortalDetectorAggregateResultInternet;
  } else if (has_redirect) {
    netval_result = Metrics::kPortalDetectorAggregateResultRedirect;
  } else if (has_suspected_redirect) {
    netval_result = Metrics::kPortalDetectorAggregateResultPartialConnectivity;
  } else {
    netval_result = Metrics::kPortalDetectorAggregateResultNoConnectivity;
  }
  metrics_->SendEnumToUMA(Metrics::kPortalDetectorAggregateResult, technology_,
                          netval_result);

  std::optional<Metrics::CapportSupported> capport_support = std::nullopt;
  if (capport_dhcp_supported_ && capport_ra_supported_) {
    capport_support = Metrics::kCapportSupportedByDHCPv4AndRA;
  } else if (capport_dhcp_supported_) {
    capport_support = Metrics::kCapportSupportedByDHCPv4;
  } else if (capport_ra_supported_) {
    capport_support = Metrics::kCapportSupportedByRA;
  }

  if (capport_support) {
    metrics_->SendEnumToUMA(Metrics::kMetricCapportAdvertised, technology_,
                            *capport_support);
  }
  if (has_redirect) {
    metrics_->SendEnumToUMA(
        Metrics::kMetricCapportSupported, technology_,
        capport_support.value_or(Metrics::kCapportNotSupported));
  }
}

}  // namespace shill
