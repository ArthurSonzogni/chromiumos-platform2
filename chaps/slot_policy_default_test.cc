// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/slot_policy_default.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "pkcs11/cryptoki.h"

namespace chaps {
namespace {

using SlotPolicyDefaultTest = testing::Test;

TEST_F(SlotPolicyDefaultTest, AcceptsRegularObjects) {
  SlotPolicyDefault slot_policy_default;
  EXPECT_TRUE(
      slot_policy_default.IsObjectClassAllowedForNewObject(CKO_CERTIFICATE));
  EXPECT_TRUE(slot_policy_default.IsObjectClassAllowedForImportedObject(
      CKO_CERTIFICATE));
}

}  // namespace
}  // namespace chaps
