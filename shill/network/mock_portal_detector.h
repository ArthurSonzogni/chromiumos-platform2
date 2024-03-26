// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_PORTAL_DETECTOR_H_
#define SHILL_NETWORK_MOCK_PORTAL_DETECTOR_H_

#include "shill/network/portal_detector.h"

#include <vector>

#include <gmock/gmock.h>

namespace shill {

class MockPortalDetector : public PortalDetector {
 public:
  MockPortalDetector();
  MockPortalDetector(const MockPortalDetector&) = delete;
  MockPortalDetector& operator=(const MockPortalDetector&) = delete;
  ~MockPortalDetector() override;

  MOCK_METHOD(void,
              Start,
              (bool http_only,
               net_base::IPFamily,
               const std::vector<net_base::IPAddress>&,
               ResultCallback),
              (override));
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
  MOCK_METHOD(int, attempt_count, (), (const, override));
};

}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_PORTAL_DETECTOR_H_
