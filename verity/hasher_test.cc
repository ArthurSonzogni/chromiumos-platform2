// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <gtest/gtest.h>

#include "verity/blake2b/blake2b-testdata.h"
#include "verity/hasher.h"

namespace verity {

namespace {

bool hash_helper(const uint8_t* in, size_t inlen, uint8_t* out, size_t outlen) {
  Blake2bHasher hasher(outlen);
  if (!hasher.Init()) return false;
  hasher.Update(in, inlen);
  hasher.Final(out);
  return true;
}

}  // namespace

TEST(Blake2bHasherTest, SimpleTest_512) {
  std::vector<uint8_t> in;
  std::vector<uint8_t> digest(BLAKE2B_OUTBYTES);

  for (int i = 0; i < BLAKE2_KAT_LENGTH; i++) {
    hash_helper(in.data(), i, digest.data(), BLAKE2B_OUTBYTES);
    ASSERT_TRUE(!memcmp(digest.data(), blake2b_kat[i], BLAKE2B_OUTBYTES));
    in.push_back((uint8_t)(i & 0xFF));
  }
}

}  // namespace verity
