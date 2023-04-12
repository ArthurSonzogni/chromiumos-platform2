// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/address_utils.h"

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

TEST(AddressUtilsTest, ValidPublicAddressOui) {
  EXPECT_TRUE(ValidatePeripheralAddress("24:E5:0F:AC:73:29", "public"));
}

TEST(AddressUtilsTest, ValidPublicAddressCid) {
  EXPECT_TRUE(ValidatePeripheralAddress("DA:A1:19:AC:73:29", "public"));
}

TEST(AddressUtilsTest, ValidPublicAddressKnownException) {
  EXPECT_TRUE(ValidatePeripheralAddress("52:54:4C:92:34:70", "public"));
}

TEST(AddressUtilsTest, InvalidPublicAddress) {
  EXPECT_FALSE(ValidatePeripheralAddress("56:54:4C:92:34:70", "public"));
}

TEST(AddressUtilsTest, InvalidPublicAddressWrongFormat) {
  EXPECT_FALSE(ValidatePeripheralAddress("WRONG_ADDRESS_FORMAT", "public"));
}

TEST(AddressUtilsTest, ValidRandomAddress) {
  EXPECT_TRUE(ValidatePeripheralAddress("70:88:6B:92:34:70", "random"));
}

TEST(AddressUtilsTest, InvalidRandomAddressWrongFormat) {
  EXPECT_FALSE(ValidatePeripheralAddress("WRONG_ADDRESS_FORMAT", "random"));
}

TEST(AddressUtilsTest, InvalidAddressType) {
  EXPECT_FALSE(
      ValidatePeripheralAddress("70:88:6B:92:34:70", "WRONG_ADDRESS_TYPE"));
}

}  // namespace
}  // namespace diagnostics
