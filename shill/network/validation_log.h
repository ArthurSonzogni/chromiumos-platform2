// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_VALIDATION_LOG_H_
#define SHILL_NETWORK_VALIDATION_LOG_H_

#include <vector>

#include <base/time/time.h>

#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/network/capport_proxy.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/technology.h"

namespace shill {

// Helper struct which keeps a history of network validation results over time
// until network validation stops for the first time or until the Network
// disconnect.
class ValidationLog {
 public:
  ValidationLog(Technology technology, Metrics* metrics);
  virtual ~ValidationLog();

  mockable void AddPortalDetectorResult(const PortalDetector::Result& result);
  void AddCAPPORTStatus(const CapportStatus& status);
  mockable void SetCapportDHCPSupported();
  mockable void SetCapportRASupported();
  mockable void SetHasTermsAndConditions();
  mockable void RecordMetrics() const;

 private:
  // Summary of a CAPPORT::Status event.
  struct CAPPORTResultData {
    base::TimeTicks timestamp = base::TimeTicks();
    bool is_captive = false;
    bool has_user_portal_url = false;
  };

  // Summary of a PortalDetector::Result event.
  struct ProbeResultData {
    base::TimeTicks timestamp = base::TimeTicks();
    PortalDetector::ValidationState validation_state =
        PortalDetector::ValidationState::kNoConnectivity;
    Metrics::PortalDetectorResult metric_result =
        Metrics::kPortalDetectorResultUnknown;
  };

  // Used internally to RecordMetrics to share the result of aggregating portal
  // detector probe events.
  struct ProbeAggregateResult {
    int total_attempts = 0;
    bool has_internet = false;
    bool has_redirect = false;
    bool has_suspected_redirect = false;
  };

  // Records metrics related to CAPPORT query results, also taking into account
  // whether portal detection probes could confirm Internet connectivity or not.
  void RecordCAPPORTMetrics(bool has_internet_connectivity) const;
  // Records metrics related to portal detector probes and returns the aggregate
  // portal detector probe result.
  ProbeAggregateResult RecordProbeMetrics() const;

  Technology technology_;
  Metrics* metrics_;
  base::TimeTicks connection_start_;
  std::vector<ProbeResultData> probe_results_;
  std::vector<CAPPORTResultData> capport_results_;
  bool capport_dhcp_supported_ = false;
  bool capport_ra_supported_ = false;
  bool has_terms_and_conditions_ = false;
};

}  // namespace shill

#endif  // SHILL_NETWORK_VALIDATION_LOG_H_
