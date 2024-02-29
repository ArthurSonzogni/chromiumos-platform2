// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_THROTTLER_H_
#define SHILL_NETWORK_MOCK_THROTTLER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/network/throttler.h"

namespace shill {

class MockThrottler : public Throttler {
 public:
  MockThrottler();
  ~MockThrottler() override;

  MOCK_METHOD(bool,
              DisableThrottlingOnAllInterfaces,
              (ResultCallback, const std::vector<std::string>&),
              (override));
  MOCK_METHOD(
      bool,
      ThrottleInterfaces,
      (ResultCallback, uint32_t, uint32_t, const std::vector<std::string>&),
      (override));
  MOCK_METHOD(bool,
              ApplyThrottleToNewInterface,
              (const std::string&),
              (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_THROTTLER_H_
