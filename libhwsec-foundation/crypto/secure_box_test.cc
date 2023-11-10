// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/secure_box.h"

#include <optional>

#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_foundation {

TEST(SecureBoxTest, DeriveKeyPairFromSeed) {
  brillo::SecureBlob seed;
  EXPECT_TRUE(brillo::SecureBlob::HexStringToSecureBlob("DEADBEEF", &seed));
  std::optional<secure_box::KeyPair> key_pair =
      secure_box::DeriveKeyPairFromSeed(seed);
  ASSERT_TRUE(key_pair.has_value());
  EXPECT_EQ(
      base::HexEncode(key_pair->public_key.data(), key_pair->public_key.size()),
      "0492B36A9A2FCF1398328C3E6ECA6D5D3D952930E8833319167A31BF3313CA15BD"
      "D9B29C4D323062BD23330CBF58631116C5373FF5A90D791DBB197E56A6FF49B3");
  EXPECT_EQ(base::HexEncode(key_pair->private_key.data(),
                            key_pair->private_key.size()),
            "00000000000000000000000000000000000000000000000000000000DEADBEF004"
            "92B36A9A2FCF1398328C3E6ECA6D5D3D952930E8833319167A31BF3313CA15BDD9"
            "B29C4D323062BD23330CBF58631116C5373FF5A90D791DBB197E56A6FF49B3");
}

}  // namespace hwsec_foundation
