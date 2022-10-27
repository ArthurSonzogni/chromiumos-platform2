// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_property_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vtpm {

namespace {

using ::testing::ElementsAre;
using ::testing::Eq;

constexpr trunks::TPM_CC kFakeCC1 = 1;
constexpr trunks::TPM_CC kFakeCC2 = 2;
constexpr trunks::TPM_CC kFakeCC3 = 3;

constexpr trunks::TPMS_TAGGED_PROPERTY kFakeProp1 = {1, 2};
constexpr trunks::TPMS_TAGGED_PROPERTY kFakeProp2 = {2, 2};
constexpr trunks::TPMS_TAGGED_PROPERTY kFakeProp3 = {5, 0};

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

TEST_F(RealTpmPropertyManagerTest, CapabilityPropertyList) {
  EXPECT_TRUE(tpm_property_manager_.GetCapabilityPropertyList().empty());
  // Add in decreasing order.
  tpm_property_manager_.AddCapabilityProperty(kFakeProp3.property,
                                              kFakeProp3.value);
  tpm_property_manager_.AddCapabilityProperty(kFakeProp2.property,
                                              kFakeProp2.value);
  tpm_property_manager_.AddCapabilityProperty(kFakeProp1.property,
                                              kFakeProp1.value);

  const std::vector<trunks::TPMS_TAGGED_PROPERTY>& props =
      tpm_property_manager_.GetCapabilityPropertyList();

  EXPECT_THAT(props[0].property, Eq(kFakeProp1.property));
  EXPECT_THAT(props[0].value, Eq(kFakeProp1.value));

  EXPECT_THAT(props[1].property, Eq(kFakeProp2.property));
  EXPECT_THAT(props[1].value, Eq(kFakeProp2.value));

  EXPECT_THAT(props[2].property, Eq(kFakeProp3.property));
  EXPECT_THAT(props[2].value, Eq(kFakeProp3.value));
}

}  // namespace

}  // namespace vtpm
