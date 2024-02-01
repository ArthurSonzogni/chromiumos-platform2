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
  explicit MockPortalDetector(ResultCallback callback);
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
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));

  void SendResult(const Result& result);

 private:
  ResultCallback callback_;
};

class MockPortalDetectorFactory : public PortalDetectorFactory {
 public:
  MockPortalDetectorFactory();
  ~MockPortalDetectorFactory() override;

  MOCK_METHOD(std::unique_ptr<PortalDetector>,
              Create,
              (EventDispatcher*,
               const PortalDetector::ProbingConfiguration&,
               PortalDetector::ResultCallback),
              (override));
};

}  // namespace shill
#endif  // SHILL_MOCK_PORTAL_DETECTOR_H_
