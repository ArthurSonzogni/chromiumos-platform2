// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/containers/span.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/ip_address.h>
#include <gtest/gtest.h>

#include "patchpanel/bpf/unit_test_utils.h"

extern "C" int match_dtls_srtp(struct __sk_buff* skb);

namespace patchpanel {
namespace {

using net_base::IPFamily;

// IP packet payload (IP header is not included) of a client hello packet of
// UDP-DTLS. Captured from a random Google Meet connection.
constexpr uint8_t kPayloadUDP[] = {
    0xb6, 0xd9, 0x4b, 0x69, 0x00, 0xa5, 0x62, 0x5d, 0x16, 0xfe, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00,
    0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0xfe, 0xfd, 0x39,
    0x97, 0xec, 0xea, 0x91, 0xdc, 0x9a, 0x84, 0x4f, 0xb1, 0x7d, 0xeb, 0x22,
    0x4d, 0xf8, 0x66, 0xac, 0xd1, 0xe0, 0xb1, 0xd0, 0xb2, 0x25, 0xbd, 0x7b,
    0x26, 0xaf, 0x55, 0x5c, 0xfb, 0x73, 0xd5, 0x00, 0x00, 0x00, 0x16, 0xc0,
    0x2b, 0xc0, 0x2f, 0xcc, 0xa9, 0xcc, 0xa8, 0xc0, 0x09, 0xc0, 0x13, 0xc0,
    0x0a, 0xc0, 0x14, 0x00, 0x9c, 0x00, 0x2f, 0x00, 0x35, 0x01, 0x00, 0x00,
    0x44, 0x00, 0x17, 0x00, 0x00, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00, 0x0a,
    0x00, 0x08, 0x00, 0x06, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18, 0x00, 0x0b,
    0x00, 0x02, 0x01, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x14,
    0x00, 0x12, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05, 0x03, 0x08, 0x05,
    0x05, 0x01, 0x08, 0x06, 0x06, 0x01, 0x02, 0x01, 0x00, 0x0e, 0x00, 0x09,
    0x00, 0x06, 0x00, 0x01, 0x00, 0x08, 0x00, 0x07, 0x00,
};

// IP packet payload (IP header is not included) of a client hello packet of
// UDP-STUN-DTLS. Captured from a random Google Meet connection.
constexpr uint8_t kPayloadSTUN[] = {
    0x84, 0xef, 0x0d, 0x96, 0x00, 0xcc, 0xfb, 0x74, 0x00, 0x16, 0x00, 0xb0,
    0x21, 0x12, 0xa4, 0x42, 0x63, 0x34, 0x66, 0x56, 0x65, 0x39, 0x46, 0x77,
    0x32, 0x75, 0x69, 0x66, 0x00, 0x12, 0x00, 0x08, 0x00, 0x01, 0xfd, 0x86,
    0x2b, 0x1d, 0x24, 0x10, 0x00, 0x13, 0x00, 0x9d, 0x16, 0xfe, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00,
    0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0xfe, 0xfd, 0xb5,
    0xd5, 0x9f, 0xa7, 0xf2, 0xd9, 0x88, 0xee, 0x85, 0x76, 0x5a, 0xf9, 0x56,
    0x8b, 0x98, 0x35, 0x0a, 0x5d, 0x60, 0xfd, 0x3a, 0xd9, 0x92, 0x18, 0xf6,
    0xcc, 0xde, 0xf2, 0xb4, 0xf8, 0x19, 0x47, 0x00, 0x00, 0x00, 0x16, 0xc0,
    0x2b, 0xc0, 0x2f, 0xcc, 0xa9, 0xcc, 0xa8, 0xc0, 0x09, 0xc0, 0x13, 0xc0,
    0x0a, 0xc0, 0x14, 0x00, 0x9c, 0x00, 0x2f, 0x00, 0x35, 0x01, 0x00, 0x00,
    0x44, 0x00, 0x17, 0x00, 0x00, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00, 0x0a,
    0x00, 0x08, 0x00, 0x06, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18, 0x00, 0x0b,
    0x00, 0x02, 0x01, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x14,
    0x00, 0x12, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05, 0x03, 0x08, 0x05,
    0x05, 0x01, 0x08, 0x06, 0x06, 0x01, 0x02, 0x01, 0x00, 0x0e, 0x00, 0x09,
    0x00, 0x06, 0x00, 0x01, 0x00, 0x08, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
};

std::vector<uint8_t> CreateUDPPacket(IPFamily family,
                                     base::span<const uint8_t> ip_payload) {
  std::vector<uint8_t> ret;

  struct iphdr ipv4_hdr = {.protocol = IPPROTO_UDP};
  struct ipv6hdr ipv6_hdr = {.nexthdr = IPPROTO_UDP};

  switch (family) {
    case IPFamily::kIPv4:
      ret = net_base::byte_utils::ToBytes(ipv4_hdr);
      break;
    case IPFamily::kIPv6:
      ret = net_base::byte_utils::ToBytes(ipv6_hdr);
      break;
  }
  ret.insert(ret.end(), ip_payload.begin(), ip_payload.end());
  return ret;
}

struct __sk_buff CreateSkBuff(IPFamily family, base::span<uint8_t> packet) {
  struct __sk_buff sk;
  sk.data = packet.data();
  sk.len = static_cast<uint32_t>(packet.size());

  switch (family) {
    case IPFamily::kIPv4:
      sk.protocol = bpf_htons(0x0800);
      break;
    case IPFamily::kIPv6:
      sk.protocol = bpf_htons(0x86DD);
      break;
  }

  return sk;
}

TEST(WebRTCDetectorTest, MatchIPv4UDP) {
  auto packet = CreateUDPPacket(IPFamily::kIPv4, kPayloadUDP);
  auto sk_buff = CreateSkBuff(IPFamily::kIPv4, packet);
  EXPECT_TRUE(match_dtls_srtp(&sk_buff));
}

TEST(WebRTCDetectorTest, MatchIPv4STUN) {
  auto packet = CreateUDPPacket(IPFamily::kIPv4, kPayloadSTUN);
  auto sk_buff = CreateSkBuff(IPFamily::kIPv4, packet);
  EXPECT_TRUE(match_dtls_srtp(&sk_buff));
}

TEST(WebRTCDetectorTest, MatchIPv6UDP) {
  auto packet = CreateUDPPacket(IPFamily::kIPv6, kPayloadUDP);
  auto sk_buff = CreateSkBuff(IPFamily::kIPv6, packet);
  EXPECT_TRUE(match_dtls_srtp(&sk_buff));
}

TEST(WebRTCDetectorTest, MatchIPv6STUN) {
  auto packet = CreateUDPPacket(IPFamily::kIPv6, kPayloadSTUN);
  auto sk_buff = CreateSkBuff(IPFamily::kIPv6, packet);
  EXPECT_TRUE(match_dtls_srtp(&sk_buff));
}

}  // namespace
}  // namespace patchpanel
