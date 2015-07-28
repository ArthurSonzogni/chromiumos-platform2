// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/mock_tpm_initializer.h"

using testing::Return;

namespace tpm_manager {

MockTpmInitializer::MockTpmInitializer() {
  ON_CALL(*this, InitializeTpm()).WillByDefault(Return(true));
}
MockTpmInitializer::~MockTpmInitializer() {}

}  // namespace tpm_manager
