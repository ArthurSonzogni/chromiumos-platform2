// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_TIME_H_
#define SHILL_MOCK_TIME_H_

#include "shill/time.h"

#include <gmock/gmock.h>

namespace shill {

class MockTime : public Time {
 public:
  MockTime();
  MockTime(const MockTime&) = delete;
  MockTime& operator=(const MockTime&) = delete;

  ~MockTime() override;

  MOCK_METHOD(bool, GetSecondsBoottime, (time_t*), (override));
  MOCK_METHOD(int, GetTimeMonotonic, (struct timeval*), (override));
  MOCK_METHOD(int, GetTimeBoottime, (struct timeval*), (override));
  MOCK_METHOD(int,
              GetTimeOfDay,
              (struct timeval*, struct timezone*),
              (override));
  MOCK_METHOD(Timestamp, GetNow, (), (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_TIME_H_
