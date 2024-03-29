// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_MOCK_VPN_METRICS_H_
#define SHILL_VPN_MOCK_VPN_METRICS_H_

#include <gmock/gmock.h>

#include "shill/vpn/vpn_metrics.h"

namespace shill {

class MockVPNDriverMetrics : public VPNDriverMetrics {
 public:
  MockVPNDriverMetrics();
  ~MockVPNDriverMetrics();

  MOCK_METHOD(void,
              ReportIPType,
              (const net_base::NetworkConfig& network_config),
              (const override));
};

}  // namespace shill

#endif  // SHILL_VPN_MOCK_VPN_METRICS_H_
