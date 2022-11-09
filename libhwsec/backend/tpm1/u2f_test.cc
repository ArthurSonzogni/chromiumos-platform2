// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::IsOkAndHolds;

namespace hwsec {

class BackendU2fTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendU2fTpm1Test, IsEnabled) {
  EXPECT_THAT(middleware_->CallSync<&Backend::U2f::IsEnabled>(),
              IsOkAndHolds(false));
}

}  // namespace hwsec
