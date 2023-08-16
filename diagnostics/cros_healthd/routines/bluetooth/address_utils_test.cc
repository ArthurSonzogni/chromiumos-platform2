// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/address_utils.h"

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

TEST(AddressUtilsTest, ValidPublicAddressOui) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("24:E5:0F:AC:73:29", "public");
  EXPECT_TRUE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, ValidPublicAddressCid) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("DA:A1:19:AC:73:29", "public");
  EXPECT_TRUE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, ValidPublicAddressKnownException) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("52:54:4C:92:34:70", "public");
  EXPECT_TRUE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, InvalidPublicAddress) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("56:54:4C:92:34:70", "public");
  EXPECT_FALSE(is_address_valid);
  EXPECT_EQ(failed_manufacturer_id, "56:54:4C");
}

TEST(AddressUtilsTest, InvalidPublicAddressWrongFormat) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("WRONG_ADDRESS_FORMAT", "public");
  EXPECT_FALSE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, ValidRandomAddress) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("70:88:6B:92:34:70", "random");
  EXPECT_TRUE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, InvalidRandomAddressWrongFormat) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("WRONG_ADDRESS_FORMAT", "random");
  EXPECT_FALSE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

TEST(AddressUtilsTest, InvalidAddressType) {
  const auto& [is_address_valid, failed_manufacturer_id] =
      ValidatePeripheralAddress("70:88:6B:92:34:70", "WRONG_ADDRESS_TYPE");
  EXPECT_FALSE(is_address_valid);
  EXPECT_FALSE(failed_manufacturer_id.has_value());
}

}  // namespace
}  // namespace diagnostics
