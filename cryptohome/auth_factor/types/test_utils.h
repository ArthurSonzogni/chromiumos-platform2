// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_TEST_UTILS_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_TEST_UTILS_H_

#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"

namespace cryptohome {

// Helper methods and common constants for writing metadata-oriented tests.
class AuthFactorDriverMetadataTest : public ::testing::Test {
 protected:
  // Useful generic constants to use for labels and version metadata.
  static constexpr char kLabel[] = "some-label";
  static constexpr char kChromeosVersion[] = "1.2.3_a_b_c";
  static constexpr char kChromeVersion[] = "1.2.3.4";

  // Create a generic metadata with the given factor-specific subtype using
  // version information from the test.
  template <typename MetadataType>
  AuthFactorMetadata CreateMetadataWithType() {
    return {
        .common = {.chromeos_version_last_updated = kChromeosVersion,
                   .chrome_version_last_updated = kChromeVersion},
        .metadata = MetadataType(),
    };
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_TEST_UTILS_H_
