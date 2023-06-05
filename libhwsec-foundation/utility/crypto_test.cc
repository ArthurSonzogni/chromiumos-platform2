// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec-foundation/utility/crypto.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace hwsec_foundation {
namespace utility {

TEST(CryptoUtilityTest, CreateSecureRandomBlobBadLength) {
  static_assert(sizeof(size_t) >= sizeof(int), "size_t is smaller than int!");
  size_t int_max = static_cast<size_t>(std::numeric_limits<int>::max());
  EXPECT_EQ(CreateSecureRandomBlob(int_max + 1).size(), 0);
}

}  // namespace utility
}  // namespace hwsec_foundation
