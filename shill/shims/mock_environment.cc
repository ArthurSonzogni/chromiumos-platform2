// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shims/mock_environment.h"

#include <string>

#include <gmock/gmock.h>

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace shill::shims {

void MockEnvironment::ExpectVariable(const std::string& name,
                                     const char* value) {
  if (value) {
    EXPECT_CALL(*this, GetVariable(name, _))
        .WillOnce(DoAll(SetArgPointee<1>(value), Return(true)));
  } else {
    EXPECT_CALL(*this, GetVariable(name, _)).WillOnce(Return(false));
  }
}

}  // namespace shill::shims
