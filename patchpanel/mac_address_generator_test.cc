// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "patchpanel/mac_address_generator.h"

namespace patchpanel {

// Tests that the mac addresses created by the generator have the proper flags.
TEST(MacAddressGenerator, Flags) {
  MacAddressGenerator generator;

  const std::vector<uint8_t> addr = generator.Generate().ToBytes();
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
  net_base::MacAddress::UnorderedSet addrs;
  addrs.reserve(kNumAddresses);

  for (uint32_t i = 0; i < kNumAddresses; ++i) {
    const net_base::MacAddress addr = generator.Generate();
    EXPECT_EQ(addrs.end(), addrs.find(addr));
    addrs.insert(addr);
  }
}

TEST(MacAddressGenerator, Stable) {
  MacAddressGenerator generator1, generator2;
  std::map<uint8_t, net_base::MacAddress> addrs;
  for (uint8_t i = 0;; ++i) {
    addrs[i] = generator1.GetStable(i);
    EXPECT_EQ(static_cast<uint8_t>(0x02),
              addrs[i].ToBytes()[0] & static_cast<uint8_t>(0x02));
    EXPECT_EQ(static_cast<uint8_t>(0),
              addrs[i].ToBytes()[0] & static_cast<uint8_t>(0x01));
    if (i == 255)
      break;
  }
  EXPECT_EQ(addrs.size(), 256);
  for (const auto addr : addrs) {
    EXPECT_EQ(addr.second, generator2.GetStable(addr.first));
  }
}

}  // namespace patchpanel
