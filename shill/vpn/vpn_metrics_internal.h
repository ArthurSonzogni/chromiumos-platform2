// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_METRICS_INTERNAL_H_
#define SHILL_VPN_VPN_METRICS_INTERNAL_H_

#include "shill/metrics.h"

namespace shill {

// This namespace contains all the metrics (name and enum values) for
// VPN-specific metrics. This is an internal header used by vpn_metrics.cc. The
// main purpose of separating it out is for sharing it with the implementation
// and the unit test. This header shouldn't be included by other files.
namespace vpn_metrics_internal {

using NameByVPNType = Metrics::NameByVPNType;

// Enum defined in shill/metrics.h (Metrics::IPType).
constexpr Metrics::EnumMetric<NameByVPNType> kMetricIPType = {
    .n = NameByVPNType{"IPType"},
    .max = Metrics::kIPTypeMax,
};

}  // namespace vpn_metrics_internal
}  // namespace shill

#endif  // SHILL_VPN_VPN_METRICS_INTERNAL_H_
