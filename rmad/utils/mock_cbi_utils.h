// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_CBI_UTILS_H_
#define RMAD_UTILS_MOCK_CBI_UTILS_H_

#include "rmad/utils/cbi_utils.h"

#include <string>

#include <gmock/gmock.h>

namespace rmad {

class MockCbiUtils : public CbiUtils {
 public:
  MockCbiUtils() = default;
  ~MockCbiUtils() override = default;

  MOCK_METHOD(bool, SetCbi, (int, const std::string&, int), (override));
  MOCK_METHOD(bool, GetCbi, (int, std::string*, int), (const, override));
  MOCK_METHOD(bool, SetCbi, (int, uint64_t, int, int), (override));
  MOCK_METHOD(bool, GetCbi, (int, uint64_t*, int), (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_CBI_UTILS_H_
