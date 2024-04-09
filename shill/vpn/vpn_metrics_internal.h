// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_METRICS_INTERNAL_H_
#define SHILL_VPN_VPN_METRICS_INTERNAL_H_

#include <base/time/time.h>

#include "shill/metrics.h"

namespace shill {

// This namespace contains all the metrics (name and enum values) for
// VPN-specific metrics. This is an internal header used by vpn_metrics.cc. The
// main purpose of separating it out is for sharing it with the implementation
// and the unit test. This header shouldn't be included by other files.
namespace vpn_metrics_internal {

using NameByVPNType = Metrics::NameByVPNType;
using FixedName = Metrics::FixedName;
using VPNEnumMetric = Metrics::EnumMetric<NameByVPNType>;
using VPNHistogramMetric = Metrics::HistogramMetric<NameByVPNType>;
constexpr int kTimerHistogramNumBuckets = Metrics::kTimerHistogramNumBuckets;

// Enum defined in shill/metrics.h (Metrics::IPType).
constexpr VPNEnumMetric kMetricIPType = {
    .n = NameByVPNType{"IPType"},
    .max = Metrics::kIPTypeMax,
};

enum VpnDriver {
  kVpnDriverOpenVpn = 0,
  kVpnDriverL2tpIpsec = 1,
  kVpnDriverThirdParty = 2,
  kVpnDriverArc = 3,
  // 4 is occupied by PPTP in chrome.
  kVpnDriverWireGuard = 5,
  kVpnDriverIKEv2 = 6,
  kVpnDriverMax
};
static constexpr Metrics::EnumMetric<FixedName> kMetricVpnDriver = {
    .n = FixedName{"Network.Shill.Vpn.Driver"},
    .max = kVpnDriverMax,
};

// Timer metrics.

// Time duration from start connecting to connected.
static constexpr VPNHistogramMetric kMetricTimeConnectToConnectedMillis = {
    .n = NameByVPNType{"TimeConnectToConnectedMillis"},
    .min = 1,
    .max = base::Seconds(30).InMilliseconds(),
    .num_buckets = kTimerHistogramNumBuckets,
};

// Time duration from reconnecting (was connected before) to connected.
static constexpr VPNHistogramMetric kMetricTimeReconnectToConnectedMillis = {
    .n = NameByVPNType{"TimeReconnectToConnectedMillis"},
    .min = 1,
    .max = base::Seconds(30).InMilliseconds(),
    .num_buckets = kTimerHistogramNumBuckets,
};

// Time duration from start connecting to idle directly (without being connected
// once). This can be expected (e.g., user cancel the connection) or unexpected
// (e.g., cannot reach the VPN server).
static constexpr VPNHistogramMetric kMetricTimeConnectToIdleMillis = {
    .n = NameByVPNType{"TimeConnectToIdleMillis"},
    .min = 1,
    .max = base::Seconds(30).InMilliseconds(),
    .num_buckets = kTimerHistogramNumBuckets,
};

// Time duration from reconnecting (was connected before) to idle directly
// (without being connected once). This can be expected (e.g., user cancel the
// connection) or unexpected (e.g., cannot reach the VPN server).
static constexpr VPNHistogramMetric kMetricTimeReconnectToIdleMillis = {
    .n = NameByVPNType{"TimeReconnectToIdleMillis"},
    .min = 1,
    .max = base::Seconds(30).InMilliseconds(),
    .num_buckets = kTimerHistogramNumBuckets,
};

// Time duration from connected to idle. This can be expected (e.g., user
// disconnect the connection) or unexpected (e.g., VPN server is no longer
// reachable).
static constexpr VPNHistogramMetric kMetricTimeConnectedToDisconnectedSeconds =
    {
        .n = NameByVPNType{"TimeConnectedToDisconnectedSeconds"},
        .min = 1,
        .max = base::Hours(8).InSeconds(),
        .num_buckets = kTimerHistogramNumBuckets,
};

// Reports why a connection attempt failed (not able to establish the
// connection).
enum ConnectFailureReason {
  kConnectFailureReasonDisconnectRequest = 0,
  kConnectFailureReasonUnknown = 1,
  kConnectFailureReasonInternal = 2,
  kConnectFailureReasonNetworkChange = 3,
  kConnectFailureReasonAuth = 4,
  kConnectFailureReasonDNSLookup = 5,
  kConnectFailureReasonConnectTimeout = 6,
  kConnectFailureReasonInvalidConfig = 7,
  kConnectFailureReasonEndReasonMax
};
static constexpr VPNEnumMetric kMetricConnectFailureReason = {
    .n = NameByVPNType{"ConnectFailureReason"},
    .max = kConnectFailureReasonEndReasonMax,
};

// Reports why a VPN connection lost (no longer connected).
enum ConnectionLostReason {
  kConnectionLostReasonDisconnectRequest = 0,
  kConnectionLostReasonUnknown = 1,
  kConnectionLostReasonInternal = 2,
  kConnectionLostReasonNetworkChange = 3,
  kConnectionLostReasonReconnect = 4,
  kConnectionLostReasonEndReasonMax
};
static constexpr VPNEnumMetric kMetricConnectionLostReason = {
    .n = NameByVPNType{"ConnectionLostReason"},
    .max = kConnectionLostReasonEndReasonMax,
};

}  // namespace vpn_metrics_internal
}  // namespace shill

#endif  // SHILL_VPN_VPN_METRICS_INTERNAL_H_
