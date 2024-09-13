// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_system.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace patchpanel {

using ::testing::Return;

MockSystem::MockSystem() {
  ON_CALL(*this, SysNetSet).WillByDefault(Return(true));
  ON_CALL(*this, WriteConfigFile).WillByDefault(Return(true));
}

MockSystem::~MockSystem() = default;

}  // namespace patchpanel
