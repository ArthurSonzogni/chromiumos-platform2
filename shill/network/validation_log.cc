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

void ValidationLog::AddResult(const NetworkMonitor::Result& result) {
  // Make sure that the total memory taken by ValidationLog is bounded.
  static constexpr size_t kValidationLogMaxSize = 128;
  if (results_.size() < kValidationLogMaxSize) {
    results_.emplace_back(base::TimeTicks::Now(), result.GetValidationState());
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

  bool has_internet = false;
  bool has_redirect = false;
  bool has_suspected_redirect = false;
  base::TimeDelta time_to_internet;
  base::TimeDelta time_to_redirect;
  base::TimeDelta time_to_internet_after_redirect;
  for (const auto& [time, result] : results_) {
    switch (result) {
      case PortalDetector::ValidationState::kNoConnectivity:
        break;
      case PortalDetector::ValidationState::kPortalSuspected:
        has_suspected_redirect = true;
        break;
      case PortalDetector::ValidationState::kPortalRedirect:
        if (!has_redirect) {
          time_to_redirect = time - connection_start_;
        }
        has_redirect = true;
        break;
      case PortalDetector::ValidationState::kInternetConnectivity:
        if (!has_internet && !has_redirect) {
          time_to_internet = time - connection_start_;
        }
        if (!has_internet && has_redirect) {
          time_to_internet_after_redirect = time - connection_start_;
        }
        has_internet = true;
        break;
    }
    // Ignores all results after the first kInternetConnectivity result.
    if (has_internet) {
      break;
    }
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

  if (time_to_internet.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorTimeToInternet, technology_,
                        time_to_internet.InMilliseconds());
  }
  if (time_to_redirect.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorTimeToRedirect, technology_,
                        time_to_redirect.InMilliseconds());
  }
  if (time_to_internet_after_redirect.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorTimeToInternetAfterRedirect,
                        technology_,
                        time_to_internet_after_redirect.InMilliseconds());
  }

  std::optional<Metrics::CapportSupported> capport_support = std::nullopt;
  if (capport_dhcp_supported_ && capport_ra_supported_) {
    capport_support = Metrics::kCapportSupportedByDHCPv4AndRA;
  } else if (capport_dhcp_supported_) {
    capport_support = Metrics::kCapportSupportedByDHCPv4;
  } else if (capport_ra_supported_) {
    capport_support = Metrics::kCapportSupportedByRA;
  }

  if (capport_support) {
    metrics_->SendEnumToUMA(Metrics::kMetricCapportAdvertised,
                            *capport_support);
  }
  if (has_redirect) {
    metrics_->SendEnumToUMA(
        Metrics::kMetricCapportSupported,
        capport_support.value_or(Metrics::kCapportNotSupported));
  }
}

}  // namespace shill
