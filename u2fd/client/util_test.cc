// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/client/util.h"

#include <optional>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

namespace u2f {
namespace util {
namespace {

brillo::Blob HexArrayToBlob(const char* array) {
  brillo::Blob blob;
  CHECK(base::HexStringToBytes(array, &blob));
  return blob;
}

// A real cert returned from Cr50 with SN field randomized.
constexpr char kCertHex[] =
    "3082013a3081e1a003020102020e04f3ef5b229bf8bbbe9930959734300a06082a8648ce3d"
    "04030230123110300e06035504031307746b2d783030313022180f32303030303130313030"
    "303030305a180f32303939313233313233353935395a30123110300e06035504031307746b"
    "2d783030313059301306072a8648ce3d020106082a8648ce3d0301070342000467b32d3439"
    "c50c03c15b76e3b763dea60a79c0b4c62d485188d57d8fe50a065526d6973ab35e0541ce90"
    "7b8947dfbed5ec8b84216e192ae23b4805d5583b85d4a31730153013060b2b0601040182e5"
    "1c020101040403020520300a06082a8648ce3d0403020348003045022100c04b0219bf623a"
    "5f4898b669310ea3c864a6f72c4962ff432ed8147d4b30c5bb02201a7682e8086b4a04228e"
    "821f51b32a7c0bec938be348d6a87b454c390cb283dc";
constexpr char kSerialNumberHex[] = "04f3ef5b229bf8bbbe9930959734";

TEST(U2fUtilTest, ParseSerialNumberFromCert) {
  std::optional<brillo::Blob> sn =
      ParseSerialNumberFromCert(HexArrayToBlob(kCertHex));
  ASSERT_TRUE(sn.has_value());
  EXPECT_EQ(*sn, HexArrayToBlob(kSerialNumberHex));
}

}  // namespace
}  // namespace util
}  // namespace u2f
