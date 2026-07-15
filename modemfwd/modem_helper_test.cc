// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_helper.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace modemfwd {

TEST(ModemHelperTest, SanitizeFirmwareRevision) {
  EXPECT_EQ(SanitizeFirmwareRevision("1.2.3.4"), "1.2.3.4");

  // Revisions spanning multiple lines with \r or \n are normalized.
  std::string multiline_revision =
      "REV_1\r\ncarrier_uuid:custom_uuid\ncarrier:custom_carrier\noem:custom_"
      "oem";
  EXPECT_EQ(
      SanitizeFirmwareRevision(multiline_revision),
      "REV_1carrier_uuid:custom_uuidcarrier:custom_carrieroem:custom_oem");
}

TEST(ModemHelperTest, ParseFirmwareInfoValid) {
  FirmwareInfo info;
  std::string helper_output =
      "main:1.2.3.4\n"
      "carrier:CARRIER_10\n"
      "carrier_uuid:UUID-ABCD-1234\n"
      "oem:OEM_VER_2\n"
      "ap:ASSOC_AP_1\n";

  EXPECT_TRUE(ParseFirmwareInfo(helper_output, &info));
  EXPECT_EQ(info.main_version, "1.2.3.4");
  EXPECT_EQ(info.carrier_version, "CARRIER_10");
  EXPECT_EQ(info.carrier_uuid, "UUID-ABCD-1234");
  EXPECT_EQ(info.oem_version, "OEM_VER_2");
  EXPECT_EQ(info.assoc_versions.at("ap"), "ASSOC_AP_1");
}

TEST(ModemHelperTest, ParseFirmwareInfoMalformed) {
  FirmwareInfo info;
  EXPECT_FALSE(ParseFirmwareInfo("", &info));
}

}  // namespace modemfwd
