// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_VALIDATION_LOG_H_
#define SHILL_NETWORK_VALIDATION_LOG_H_

#include <utility>
#include <vector>

#include <base/time/time.h>

#include "shill/metrics.h"
#include "shill/network/network_monitor.h"
#include "shill/portal_detector.h"
#include "shill/technology.h"

namespace shill {

// Helper struct which keeps a history of network validation results over time
// until network validation stops for the first time or until the Network
// disconnect.
class ValidationLog {
 public:
  ValidationLog(Technology technology, Metrics* metrics);
  void AddResult(const NetworkMonitor::Result& result);
  void SetCapportDHCPSupported();
  void SetCapportRASupported();
  void RecordMetrics() const;

 private:
  Technology technology_;
  Metrics* metrics_;
  base::TimeTicks connection_start_;
  std::vector<std::pair<base::TimeTicks, PortalDetector::ValidationState>>
      results_;
  bool capport_dhcp_supported_ = false;
  bool capport_ra_supported_ = false;
};

}  // namespace shill

#endif  // SHILL_NETWORK_VALIDATION_LOG_H_
