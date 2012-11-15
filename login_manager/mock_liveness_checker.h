// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_LIVENESS_CHECKER_H_
#define LOGIN_MANAGER_MOCK_LIVENESS_CHECKER_H_

#include "login_manager/liveness_checker.h"

#include <base/basictypes.h>
#include <gmock/gmock.h>

namespace login_manager {

class MockLivenessChecker : public LivenessChecker {
 public:
  MockLivenessChecker();
  virtual ~MockLivenessChecker();

  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(HandleLivenessConfirmed, void());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(IsRunning, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockLivenessChecker);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_LIVENESS_CHECKER_H_
