// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_property_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vtpm {

namespace {

using ::testing::ElementsAre;

constexpr trunks::TPM_CC kFakeCC1 = 1;
constexpr trunks::TPM_CC kFakeCC2 = 2;
constexpr trunks::TPM_CC kFakeCC3 = 3;

}  // namespace

class RealTpmPropertyManagerTest : public testing::Test {
 protected:
  RealTpmPropertyManager tpm_property_manager_;
};

namespace {

TEST_F(RealTpmPropertyManagerTest, CommandList) {
  EXPECT_TRUE(tpm_property_manager_.GetCommandList().empty());
  // Add the commands in decreasing order.
  tpm_property_manager_.AddCommand(kFakeCC3);
  tpm_property_manager_.AddCommand(kFakeCC2);
  tpm_property_manager_.AddCommand(kFakeCC1);

  // Add duplicates.
  tpm_property_manager_.AddCommand(kFakeCC1);
  tpm_property_manager_.AddCommand(kFakeCC2);
  tpm_property_manager_.AddCommand(kFakeCC2);
  tpm_property_manager_.AddCommand(kFakeCC3);

  EXPECT_THAT(tpm_property_manager_.GetCommandList(),
              ElementsAre(kFakeCC1, kFakeCC2, kFakeCC3));
}

}  // namespace

}  // namespace vtpm
