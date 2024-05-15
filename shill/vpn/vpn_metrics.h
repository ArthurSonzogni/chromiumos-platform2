// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_METRICS_H_
#define SHILL_VPN_VPN_METRICS_H_

#include <base/time/time.h>
#include <net-base/network_config.h>

#include "shill/mockable.h"
#include "shill/vpn/vpn_end_reason.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

class Metrics;

// Helper class to report the metrics for a specific VPN type (VPNDriver). The
// metrics defined in this class are the common metrics for all VPN types.
class VPNDriverMetrics {
 public:
  explicit VPNDriverMetrics(Metrics* metrics, VPNType vpn_type);
  virtual ~VPNDriverMetrics();

  VPNDriverMetrics(const VPNDriverMetrics&) = delete;
  VPNDriverMetrics& operator=(const VPNDriverMetrics&) = delete;

  // Reports the datapath metrics according to `network_config`.
  mockable void ReportNetworkConfig(
      const net_base::NetworkConfig& network_config) const;

  // The following functions changes the connection state kept by this metrics
  // object. The state machine and allowed transition are shown as following
  // figure.
  //
  //   idle ---> connecting ---> connected <---> reconnecting
  //     ^           |              |                 |
  //     |           |              |                 |
  //     +-----------+--------------+-----------------+
  //
  // Corresponding metrics will be reported on the state changes. See the
  // implementation for details.
  mockable void ReportConnecting();
  mockable void ReportConnected();
  mockable void ReportReconnecting();
  mockable void ReportDisconnected(VPNEndReason reason);

 private:
  enum class ConnectionState {
    kIdle,
    kConnecting,
    kReconnecting,
    kConnected,
  };

  // Update `connection_state_` to `new_state`, and returns the time duration
  // after the last time this function is called.
  base::TimeDelta SetConnectionState(ConnectionState new_state);

  Metrics* metrics_;

  VPNType vpn_type_;
  ConnectionState connection_state_ = ConnectionState::kIdle;
  base::TimeTicks connection_state_last_changed_at_;
};

class VPNGeneralMetrics {
 public:
  explicit VPNGeneralMetrics(Metrics* metrics) : metrics_(metrics) {}

  VPNGeneralMetrics(const VPNGeneralMetrics&) = delete;
  VPNGeneralMetrics& operator=(const VPNGeneralMetrics&) = delete;

  void ReportServicesNumber(int num);

 private:
  Metrics* metrics_;
};

// TODO(b/331743444): Add other classes to report driver-specific metrics.

}  // namespace shill

#endif  // SHILL_VPN_VPN_METRICS_H_
