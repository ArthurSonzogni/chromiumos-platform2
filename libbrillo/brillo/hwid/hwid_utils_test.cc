// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/hwid/hwid_utils.h"

#include <optional>

#include <gtest/gtest.h>

namespace brillo {

TEST(DecodeHWIDTest, DecodeValidHWIDSuccess) {
  EXPECT_EQ(hwid::DecodeHWID("ZEROONE A2A-747"), "00000000000001111");
  EXPECT_EQ(hwid::DecodeHWID("SARIEN-MCOO 0-8-77-1D0 A2A-73B"),
            "00000000000001111");
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR D3A-39F-27K-E2A"),
            "00011001000001101111100101110101010101000");

  // Minimum viable HWID: 1 bit, 1 EOS, 3 bit of padding, 8 bit of checksum.
  EXPECT_EQ(hwid::DecodeHWID("ZZZ I4B"), "0");
  EXPECT_EQ(hwid::DecodeHWID("ZZZ Y3F"), "1");
}

TEST(DecodeHWIDTest, DecodeMalformedHWIDFailure) {
  EXPECT_EQ(hwid::DecodeHWID(""), std::nullopt);

  // The encoded bits are not composed of triplets.
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR"), std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR "), std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR ZZZZ"), std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR D3A-39-27K-E6B"), std::nullopt);

  // The encoded bits contain invalid characters.
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR 16F"),  // '1' in 1st pos.
            std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR YAF"),  // 'A' in 2nd pos.
            std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR Y61"),  // '1' in 3rd pos.
            std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR A2A*72D"), std::nullopt);
  EXPECT_EQ(hwid::DecodeHWID("REDRIX-ZZCR a2a-72D"), std::nullopt);

  // 13 bits but no EOS ('1') before checksum bits.
  // A6F -> 00000 100 00101. Bits before 8-bit checksum = "00000". No '1'.
  EXPECT_EQ(hwid::DecodeHWID("ZZZ A6F"), std::nullopt);
}

TEST(CalculateChecksumTest, Success) {
  EXPECT_EQ(hwid::CalculateChecksum("CHROMEBOOK ASDFQWERZXCV"), "6C");
  EXPECT_EQ(hwid::CalculateChecksum("MODEL-CODE A1B-C2D-E"), "2J");
  EXPECT_EQ(hwid::CalculateChecksum("SARIEN-MCOO 0-8-77-1D0 A2A-7"), "3B");
}

TEST(CalculateChecksumTest, Failure) {
  EXPECT_EQ(hwid::CalculateChecksum(""), std::nullopt);
  EXPECT_EQ(hwid::CalculateChecksum("MODEL"), std::nullopt);
  EXPECT_EQ(hwid::CalculateChecksum("MODEL-CODE"), std::nullopt);
}

TEST(EncodeHWIDTest, Success) {
  EXPECT_EQ(hwid::EncodeHWID("MODEL", ""), "MODEL Q64");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "0"), "ZZZ I4B");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "1"), "ZZZ Y3F");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "00000"), "ZZZ A6A-A3T");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "000000"), "ZZZ A4A-A45");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "0000000"), "ZZZ A3A-A8Y");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "00000000"), "ZZZ A2Q-A76");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "000000000"), "ZZZ A2I-A9H");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "0000000000"), "ZZZ A2E-A9L");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "00000000000"), "ZZZ A2C-A5N");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "000000000000"), "ZZZ A2B-A3M");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "0000000000000"), "ZZZ A2A-Q6L");
  EXPECT_EQ(hwid::EncodeHWID("ZZZ", "00000000000000"), "ZZZ A2A-I85");
  EXPECT_EQ(hwid::EncodeHWID("ZEROONE", "00000000000001111"),
            "ZEROONE A2A-747");
  EXPECT_EQ(hwid::EncodeHWID("SARIEN-MCOO 0-8-77-1D0", "00000000000001111"),
            "SARIEN-MCOO 0-8-77-1D0 A2A-73B");
  EXPECT_EQ(hwid::EncodeHWID("REDRIX-ZZCR",
                             "00011001000001101111100101110101010101000"),
            "REDRIX-ZZCR D3A-39F-27K-E2A");
}

TEST(EncodeHWIDTest, FailureWithMalformedBinaryString) {
  EXPECT_EQ(hwid::EncodeHWID("MODEL", "012"), std::nullopt);
  EXPECT_EQ(hwid::EncodeHWID("MODEL", "abc"), std::nullopt);
  EXPECT_EQ(hwid::EncodeHWID("MODEL", "0101 01"), std::nullopt);
}

TEST(EncodeHWIDTest, FailureWithInvalidPrefix) {
  EXPECT_EQ(hwid::EncodeHWID("", "00000"), std::nullopt);
  EXPECT_EQ(hwid::EncodeHWID("   ", "00000"), std::nullopt);
}

}  // namespace brillo
