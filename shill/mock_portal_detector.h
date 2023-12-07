// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_PORTAL_DETECTOR_H_
#define SHILL_MOCK_PORTAL_DETECTOR_H_

#include "shill/portal_detector.h"

#include <memory>
#include <string>
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
              (const std::string& ifname,
               net_base::IPFamily,
               const std::vector<net_base::IPAddress>&,
               const std::string& logging_tag),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, IsInProgress, (), (const, override));
  MOCK_METHOD(void, ResetAttemptDelays, (), (override));
};

class MockPortalDetectorFactory : public PortalDetectorFactory {
 public:
  MockPortalDetectorFactory();
  ~MockPortalDetectorFactory() override;

  MOCK_METHOD(std::unique_ptr<PortalDetector>,
              Create,
              (EventDispatcher*,
               const PortalDetector::ProbingConfiguration&,
               base::RepeatingCallback<void(const PortalDetector::Result&)>),
              (override));
};

}  // namespace shill
#endif  // SHILL_MOCK_PORTAL_DETECTOR_H_
