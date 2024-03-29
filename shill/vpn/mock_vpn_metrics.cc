// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/mock_vpn_metrics.h"

namespace shill {

MockVPNDriverMetrics::MockVPNDriverMetrics()
    : VPNDriverMetrics(/*metrics*/ nullptr, VPNType::kOpenVPN) {}

MockVPNDriverMetrics::~MockVPNDriverMetrics() = default;

}  // namespace shill
