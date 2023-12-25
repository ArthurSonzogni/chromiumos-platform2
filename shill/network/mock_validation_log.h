// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_VALIDATION_LOG_H_
#define SHILL_NETWORK_MOCK_VALIDATION_LOG_H_

#include <gmock/gmock.h>

#include "shill/network/network_monitor.h"
#include "shill/network/validation_log.h"

namespace shill {

class MockValidationLog : public ValidationLog {
 public:
  MockValidationLog();
  ~MockValidationLog() override;

  MOCK_METHOD(void, AddResult, (const NetworkMonitor::Result&), (override));
  MOCK_METHOD(void, SetCapportDHCPSupported, (), (override));
  MOCK_METHOD(void, SetCapportRASupported, (), (override));
  MOCK_METHOD(void, RecordMetrics, (), (const, override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_VALIDATION_LOG_H_
