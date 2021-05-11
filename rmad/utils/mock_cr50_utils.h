// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_CR50_UTILS_H_
#define RMAD_UTILS_MOCK_CR50_UTILS_H_

#include <string>

namespace rmad {

class MockCr50Utils {
 public:
  MockCr50Utils() = default;
  virtual ~MockCr50Utils() = default;

  MOCK_METHOD(bool, RoVerificationKeyPressed, (), (const, override));
  MOCK_METHOD(bool,
              GetRsuChallengeCode,
              (std::string * challenge_code),
              (const, override));
  MOCK_METHOD(bool,
              PerformRsu,
              (const std::string& unlock_code),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_CR50_UTILS_H_
