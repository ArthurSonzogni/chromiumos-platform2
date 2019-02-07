// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <functional>
#include <unordered_set>

#include <gtest/gtest.h>

#include "arc/network/mac_address_generator.h"

namespace arc_networkd {
namespace {
// The standard library sadly does not provide a hash function for std::array.
// So implement one here for MacAddress based off boost::hash_combine.
struct MacAddressHasher {
  size_t operator()(const MacAddress& addr) const noexcept {
    std::hash<uint8_t> hasher;

    size_t hash = 0;
    for (uint8_t octet : addr) {
      hash ^= hasher(octet) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    return hash;
  }
};

}  // namespace

// Tests that the mac addresses created by the generator have the proper flags.
TEST(MacAddressGenerator, Flags) {
  MacAddressGenerator generator;

  MacAddress addr = generator.Generate();
  EXPECT_EQ(static_cast<uint8_t>(0x02), addr[0] & static_cast<uint8_t>(0x02));
  EXPECT_EQ(static_cast<uint8_t>(0), addr[0] & static_cast<uint8_t>(0x01));
}

// Tests that the generator does not create duplicate addresses.  Obviously due
// the vast range of possible addresses it's expensive to do an exhaustive
// search in this test.  However, we can take advantage of the birthday paradox
// to reduce the number of addresses we need to generate.  We know that the 2
// least significant bits of the first octet in the address are fixed.  This
// leaves 2^46 possible addresses.  Generating 2^25 addresses gives us a 99.96%
// chance of triggering a collision in this range.  So if the generator returns
// 2^25 unique addresses then we can be fairly certain that it won't give out
// duplicate addresses.
// This test is currently disabled because it takes a long time to run
// (~minutes).  We ran it on the CQ for several months without issue so we can
// be pretty confident that the current implementation does not produce
// duplicates.  If you make any changes to the mac address generation code,
// please re-enable this test.
TEST(MacAddressGenerator, DISABLED_Duplicates) {
  constexpr uint32_t kNumAddresses = (1 << 25);

  MacAddressGenerator generator;
  std::unordered_set<MacAddress, MacAddressHasher> addrs;
  addrs.reserve(kNumAddresses);

  for (uint32_t i = 0; i < kNumAddresses; ++i) {
    MacAddress addr = generator.Generate();
    EXPECT_EQ(addrs.end(), addrs.find(addr));
    addrs.insert(addr);
  }
}

// Tests that the MacAddressGenerator rejects addresses that don't have the
// locally administered bit set.
TEST(MacAddressGenerator, LocallyAdministered) {
  MacAddressGenerator generator;

  MacAddress addr = {0xf7, 0x69, 0xe5, 0xc4, 0x1f, 0x74};
  addr[0] &= static_cast<uint8_t>(0xfd);

  EXPECT_FALSE(generator.Insert(addr));
}

// Tests that the MacAddressGenerator rejects addresses that have the multicast
// bit set.
TEST(MacAddressGenerator, Multicast) {
  MacAddressGenerator generator;

  MacAddress addr = {0xf7, 0x69, 0xe5, 0xc4, 0x1f, 0x74};
  addr[0] |= static_cast<uint8_t>(0x01);

  EXPECT_FALSE(generator.Insert(addr));
}

}  // namespace arc_networkd
