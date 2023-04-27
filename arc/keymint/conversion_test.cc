// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

namespace arc::keymint {

namespace {

// TODO(vraheja): Remove this when conversion.cc is added.
std::vector<uint8_t> ConvertFromKeymasterMessage(const uint8_t* data,
                                                 const size_t size) {
  return std::vector<uint8_t>(data, data + size);
}

}  // namespace

constexpr std::array<uint8_t, 3> kBlob1{{3, 23, 59}};

::testing::AssertionResult VerifyVectorUint8(const uint8_t* a,
                                             const size_t a_size,
                                             const std::vector<uint8_t>& b) {
  if (a_size != b.size()) {
    return ::testing::AssertionFailure()
           << "Sizes differ: a=" << a_size << " b=" << b.size();
  }
  for (size_t i = 0; i < a_size; ++i) {
    if (a[i] != b[i]) {
      return ::testing::AssertionFailure()
             << "Elements differ: a=" << static_cast<int>(a[i])
             << " b=" << static_cast<int>(b[i]);
    }
  }
  return ::testing::AssertionSuccess();
}

TEST(ConvertFromKeymasterMessage, Uint8Vector) {
  // Convert.
  std::vector<uint8_t> output =
      ConvertFromKeymasterMessage(kBlob1.data(), kBlob1.size());

  // Verify.
  EXPECT_TRUE(VerifyVectorUint8(kBlob1.data(), kBlob1.size(), output));
}

}  // namespace arc::keymint
