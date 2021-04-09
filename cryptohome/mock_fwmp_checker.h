// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_FWMP_CHECKER_H_
#define CRYPTOHOME_MOCK_FWMP_CHECKER_H_

#include <gmock/gmock.h>

namespace cryptohome {

class MockFwmpChecker : public FwmpChecker {
 public:
  MockFwmpChecker() = default;
  ~MockFwmpChecker() override = default;

  MOCK_METHOD(bool, IsValidForWrite, (uint32_t nv_index), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_FWMP_CHECKER_H_
