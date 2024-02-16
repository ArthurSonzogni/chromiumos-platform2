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

namespace {
// Maximum size of an event buffer to ensure that the total memory taken by
// ValidationLog is bounded.
static constexpr size_t kValidationLogMaxSize = 128;

}  // namespace

ValidationLog::ValidationLog(Technology technology, Metrics* metrics)
    : technology_(technology),
      metrics_(metrics),
      connection_start_(base::TimeTicks::Now()) {}

ValidationLog::~ValidationLog() = default;

void ValidationLog::AddPortalDetectorResult(
    const PortalDetector::Result& result) {
  if (probe_results_.size() < kValidationLogMaxSize) {
    probe_results_.emplace_back(base::TimeTicks::Now(),
                                result.GetValidationState(),
                                result.GetResultMetric());
  }
}

void ValidationLog::AddCAPPORTStatus(const CapportStatus& status) {
  if (capport_results_.size() < kValidationLogMaxSize) {
    capport_results_.emplace_back(base::TimeTicks::Now(), status.is_captive,
                                  status.user_portal_url.has_value());
  }
}

void ValidationLog::SetCapportDHCPSupported() {
  capport_dhcp_supported_ = true;
}

void ValidationLog::SetCapportRASupported() {
  capport_ra_supported_ = true;
}

void ValidationLog::SetHasTermsAndConditions() {
  has_terms_and_conditions_ = true;
}

ValidationLog::ProbeAggregateResult ValidationLog::RecordProbeMetrics() const {
  if (probe_results_.empty()) {
    return {};
  }

  int total_attempts = 0;
  bool has_internet = false;
  bool has_redirect = false;
  bool has_suspected_redirect = false;

  for (const auto& result_data : probe_results_) {
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

  return ProbeAggregateResult{
      .total_attempts = total_attempts,
      .has_internet = has_internet,
      // Return as true both 302/307 redirect cases and spoofed 200 answer
      // cases.
      .has_redirect = has_redirect || has_suspected_redirect,
  };
}

void ValidationLog::RecordCAPPORTMetrics(bool has_internet_connectivity) const {
  if (capport_results_.empty()) {
    return;
  }

  std::optional<bool> is_captive = std::nullopt;
  bool has_user_portal_url = false;
  for (const auto& result_data : capport_results_) {
    // Ensure |is_captive| is initialized based on the first status seen.
    if (!is_captive.has_value()) {
      if (result_data.is_captive) {
        is_captive = true;
      } else {
        // Ignore CAPPORT network connection where the captive portal was never
        // closed. This can happen if the device reconnects to the captive
        // portal network after having cleared the sign-in flow once and the
        // network remembers that the portal is open for the device.
        return;
      }
    }

    // Check if the portal is now open.
    if (*is_captive && !result_data.is_captive) {
      is_captive = false;
      const base::TimeDelta time_to_not_captive =
          result_data.timestamp - connection_start_;
      metrics_->SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTNotCaptive,
                          technology_, time_to_not_captive.InMilliseconds());

      // Ignore the user portal URL if the portal becomes open without
      // having seen first the user portal URL with is_captive==true.
      break;
    }

    // Check if the portal advertises a user portal URL.
    if (!has_user_portal_url && result_data.has_user_portal_url) {
      has_user_portal_url = true;
      const base::TimeDelta time_to_user_portal_url =
          result_data.timestamp - connection_start_;
      metrics_->SendToUMA(Metrics::kPortalDetectorTimeToCAPPORTUserPortalURL,
                          technology_,
                          time_to_user_portal_url.InMilliseconds());
    }
  }

  Metrics::AggregateCAPPORTResult capport_aggregate_result =
      Metrics::kAggregateCAPPORTResultUnknown;
  if (*is_captive) {
    capport_aggregate_result = Metrics::kAggregateCAPPORTResultCaptive;
  } else if (has_internet_connectivity) {
    capport_aggregate_result = Metrics::kAggregateCAPPORTResultOpenWithInternet;
  } else {
    capport_aggregate_result =
        Metrics::kAggregateCAPPORTResultOpenWithoutInternet;
  }
  metrics_->SendEnumToUMA(Metrics::kPortalDetectorAggregateCAPPORTResult,
                          technology_, capport_aggregate_result);
}

void ValidationLog::RecordMetrics() const {
  auto probe_aggregate_result = RecordProbeMetrics();

  RecordCAPPORTMetrics(probe_aggregate_result.has_internet);

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

  if (probe_aggregate_result.has_redirect) {
    metrics_->SendEnumToUMA(
        Metrics::kMetricCapportSupported, technology_,
        capport_support.value_or(Metrics::kCapportNotSupported));
  }

  if (technology_ == Technology::kWiFi && !probe_results_.empty()) {
    Metrics::TermsAndConditionsAggregateResult tc_result =
        Metrics::kTermsAndConditionsAggregateResultUnknown;
    if (has_terms_and_conditions_ && probe_aggregate_result.has_redirect) {
      tc_result = Metrics::kTermsAndConditionsAggregateResultPortalWithURL;
    } else if (has_terms_and_conditions_) {
      tc_result = Metrics::kTermsAndConditionsAggregateResultNoPortalWithURL;
    } else if (probe_aggregate_result.has_redirect) {
      tc_result = Metrics::kTermsAndConditionsAggregateResultPortalNoURL;
    } else {
      tc_result = Metrics::kTermsAndConditionsAggregateResultNoPortalNoURL;
    }
    metrics_->SendEnumToUMA(Metrics::kMetricTermsAndConditionsAggregateResult,
                            tc_result);
  }
}

}  // namespace shill
