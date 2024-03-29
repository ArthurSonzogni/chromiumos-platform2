// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_METRICS_H_
#define SHILL_VPN_VPN_METRICS_H_

#include <net-base/network_config.h>

#include "shill/vpn/vpn_types.h"

namespace shill {

class Metrics;

// Helper class to report the metrics for a specific VPN type (VPNDriver). The
// metrics defined in this class are the common metrics for all VPN types. This
// class is designed to be stateless -- all functions can actually be static,
// they are non-static now just for the ease of using and testing.
class VPNDriverMetrics {
 public:
  // Define ctor and dtor directly in the header file since they are trivial.
  explicit VPNDriverMetrics(Metrics* metrics, VPNType vpn_type)
      : metrics_(metrics), vpn_type_(vpn_type) {}
  ~VPNDriverMetrics() = default;

  VPNDriverMetrics(const VPNDriverMetrics&) = delete;
  VPNDriverMetrics& operator=(const VPNDriverMetrics&) = delete;

  // TODO(b/331743444): Change the name and implementation to report all metrics
  // can be inferred from network_config.
  void ReportIPType(const net_base::NetworkConfig& network_config) const;

 private:
  Metrics* metrics_;

  VPNType vpn_type_;
};

// TODO(b/331743444): Add other classes to report driver-specific metrics and
// general metrics which are not tied to a driver.

}  // namespace shill

#endif  // SHILL_VPN_VPN_METRICS_H_
