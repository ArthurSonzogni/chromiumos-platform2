// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_METRICS_INTERNAL_H_
#define SHILL_VPN_VPN_METRICS_INTERNAL_H_

#include <base/time/time.h>
#include <net-base/network_config.h>

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

// Routing setup for a specific IP family. This only considers the routing table
// setup for the VPN interface (i.e., routes in the routing table), but not the
// actual routing setup on the system (i.e., all the components affecting
// routing: rules, routes, iptables, etc.).
//
// Caveat: when we calculate whether the address space is fully covered, we will
// only check if there is a default route and no excluded route. If there is no
// /0 route but the address space is fully covered by multiple routes, it will
// be considered as kRoutingTypeSplit.
enum RoutingType {
  // The routes cover the whole address space.
  kRoutingTypeFull = 0,
  // The routes cover the address space partially.
  kRoutingTypeSplit = 1,
  // No route for this IP family.
  kRoutingTypeBypass = 2,
  // This IP family is blocked.
  kRoutingTypeBlocked = 3,

  kRoutingTypeMax,
};
constexpr VPNEnumMetric kMetricIPv4RoutingType = {
    .n = NameByVPNType{"IPv4RoutingType"},
    .max = kRoutingTypeMax,
};
constexpr VPNEnumMetric kMetricIPv6RoutingType = {
    .n = NameByVPNType{"IPv6RoutingType"},
    .max = kRoutingTypeMax,
};

// The length of the largest (shortest) prefix for {IPv4, IPv6} x {included
// routes, excluded routes}. These metrics will only be reported when the
// routing type is kRoutingTypeSplit on the corresponding IP family, but it's
// still valid that the reported value is 0 since included routes and excluded
// routes can be set at the same time.
constexpr int kPrefixLengthHistogramBucket = 8;
constexpr VPNHistogramMetric kMetricIPv4IncludedRoutesLargestPrefix = {
    .n = NameByVPNType{"IPv4IncludedRoutesLargestPrefix"},
    .min = 1,
    .max = 32,
    .num_buckets = kPrefixLengthHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv4ExcludedRoutesLargestPrefix = {
    .n = NameByVPNType{"IPv4ExcludedRoutesLargestPrefix"},
    .min = 1,
    .max = 32,
    .num_buckets = kPrefixLengthHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv6IncludedRoutesLargestPrefix = {
    .n = NameByVPNType{"IPv6IncludedRoutesLargestPrefix"},
    .min = 1,
    .max = 128,
    .num_buckets = kPrefixLengthHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv6ExcludedRoutesLargestPrefix = {
    .n = NameByVPNType{"IPv6ExcludedRoutesLargestPrefix"},
    .min = 1,
    .max = 128,
    .num_buckets = kPrefixLengthHistogramBucket,
};

// Number of included or excluded routes. Note that for a default route, it will
// always be counted as an included route, even if it is not explicitly set.
constexpr int kPrefixNumberHistogramMax = 20;
constexpr int kPrefixNumberHistogramBucket = 8;
constexpr VPNHistogramMetric kMetricIPv4IncludedRoutesNumber = {
    .n = NameByVPNType{"IPv4IncludedRoutesNumber"},
    .min = 1,
    .max = kPrefixNumberHistogramMax,
    .num_buckets = kPrefixNumberHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv4ExcludedRoutesNumber = {
    .n = NameByVPNType{"IPv4ExcludedRoutesNumber"},
    .min = 1,
    .max = kPrefixNumberHistogramMax,
    .num_buckets = kPrefixNumberHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv6IncludedRoutesNumber = {
    .n = NameByVPNType{"IPv6IncludedRoutesNumber"},
    .min = 1,
    .max = kPrefixNumberHistogramMax,
    .num_buckets = kPrefixNumberHistogramBucket,
};
constexpr VPNHistogramMetric kMetricIPv6ExcludedRoutesNumber = {
    .n = NameByVPNType{"IPv6ExcludedRoutesNumber"},
    .min = 1,
    .max = kPrefixNumberHistogramMax,
    .num_buckets = kPrefixNumberHistogramBucket,
};

// MTU value.
constexpr VPNHistogramMetric kMetricMTU = {
    .n = NameByVPNType{"MTU"},
    .min = net_base::NetworkConfig::kMinIPv4MTU,
    .max = net_base::NetworkConfig::kDefaultMTU + 1,
    .num_buckets = 50,
};

// Name servers.
enum NameServerConfig {
  kNameServerConfigNone = 0,
  kNameServerConfigIPv4Only = 1,
  kNameServerConfigIPv6Only = 2,
  kNameServerConfigDualStack = 3,

  kNameServerConfigMax,
};
constexpr VPNEnumMetric kMetricNameServers = {
    .n = NameByVPNType{"NameServers"},
    .max = kNameServerConfigMax,
};

}  // namespace vpn_metrics_internal
}  // namespace shill

#endif  // SHILL_VPN_VPN_METRICS_INTERNAL_H_
