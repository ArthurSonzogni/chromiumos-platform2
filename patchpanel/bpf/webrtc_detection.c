// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UNIT_TEST
// Include vmlinux.h at first to declare all kernel types.
#include "include/patchpanel/vmlinux/vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#else
#include "patchpanel/bpf/unit_test_utils.h"
#endif  // UNIT_TEST

// The return values used in this program. In the context of iptables, a
// non-zero return value means a match.
#define RET_IPTABLES_MATCHED (1)
#define RET_IPTABLES_NOT_MATCHED (0)

// We cannot include some kernel headers directly since it may cause a conflict
// with vmlinux.h. Define some consts directly here.
#define ETH_HLEN 14
#define ETH_P_IP 0x0800
#define ETH_P_IPV6 0x86DD
#define IPPROTO_UDP 17

#ifndef UNIT_TEST
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
// A partial copy of the definition in kernel/v4.19/include/uapi/linux/bpf.h.
// This is a HACK on 4.19 kernel: the vmlinux.h above doesn't contain the
// definition of this struct, and we also cannot include the header directly due
// to the reason mentioned above. Note that eBPF is not enabled on 4.19 kernel,
// so we haven't verified and also don't need to verify if this code works.
struct __sk_buff {
  u32 len;
  u32 pkt_type;
  u32 mark;
  u32 queue_mapping;
  u32 protocol;
  // There are more fields, but we don't care them in the current code.
};
#endif
#endif  // UNIT_TEST

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

// Notes for the implementation in this file:
// - For the helper functions, they must be marked as
//   `__attribute__((always_inline))`, otherwise the program cannot be loaded by
//   libbpf.
// - bpf_skb_load_bytes_relative() will return an error if we try to access an
//   invalid index in the packet contents, so we don't need to do the
//   out-of-bounds check before calling it.

// Assumes the packet is an IPv4 packet, parses the payload and returns the
// matching result (RET_IPTABLES_MATCHED or RET_IPTABLES_NOT_MATCHED).
static __attribute__((always_inline)) int handle_ipv4(struct __sk_buff* skb);
// Assumes the packet is an IPv6 packet, parses the payload and returns the
// matching result (RET_IPTABLES_MATCHED or RET_IPTABLES_NOT_MATCHED).
static __attribute__((always_inline)) int handle_ipv6(struct __sk_buff* skb);
// Assumes the packet is an UDP packet and the UDP header starts from
// `base_offset_to_net`, parses the payload and returns the matching result
// (RET_IPTABLES_MATCHED or RET_IPTABLES_NOT_MATCHED).
static __attribute__((always_inline)) int handle_udp(struct __sk_buff* skb,
                                                     u32 base_offset_to_net);
// Assumes the packet is an DTLS packet and the DTLS contents starts from
// `base_offset_to_net`, parses the payload and returns the matching result
// (RET_IPTABLES_MATCHED or RET_IPTABLES_NOT_MATCHED).
static __attribute__((always_inline)) int handle_dtls(struct __sk_buff* skb,
                                                      u32 base_offset_to_net);

static int handle_ipv4(struct __sk_buff* skb) {
  u8 proto;
  if (bpf_skb_load_bytes_relative(skb, offsetof(struct iphdr, protocol), &proto,
                                  sizeof(proto), BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  if (proto == IPPROTO_UDP) {
    return handle_udp(skb, sizeof(struct iphdr));
  }

  return RET_IPTABLES_NOT_MATCHED;
}

static int handle_ipv6(struct __sk_buff* skb) {
  u8 proto;
  if (bpf_skb_load_bytes_relative(skb, offsetof(struct ipv6hdr, nexthdr),
                                  &proto, sizeof(proto),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  if (proto == IPPROTO_UDP) {
    return handle_udp(skb, sizeof(struct ipv6hdr));
  }

  return RET_IPTABLES_NOT_MATCHED;
}

// The packet is a UDP packet and the UDP header start from
// `base_offset_to_net`.
static int handle_udp(struct __sk_buff* skb, u32 base_offset_to_net) {
  u32 offset = base_offset_to_net;

  // Read the UDP dst port, check if the port is the standard STUN port (3478)
  // to decide whether we need to parse the STUN payload. This has both false
  // positive and false negative: a TURN/STUN server may not use this port, and
  // this port may be used by other applications.
  const int kSTUNPort = 3478;
  u16 dport;
  if (bpf_skb_load_bytes_relative(skb, offset + offsetof(struct udphdr, dest),
                                  &dport, sizeof(dport),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  // Skip the UDP header.
  offset += sizeof(struct udphdr);

  // Assume it's not an STUN packet. Try parsing DTLS directly.
  if (dport != bpf_htons(kSTUNPort)) {
    return handle_dtls(skb, offset);
  }

  // Assume it's an STUN packet. Skip the STUN header at first.
  const int kSTUNHeaderSize = 20;
  offset += kSTUNHeaderSize;

  // The payload of a STUN packet is a list of attributes. Try to find the DATA
  // attribute which may contains the DTLS payload. See RFC 8489 for the
  // structure of the STUN packet in detail.
  const int kDataAttributeType = 0x13;

  // Assume that the DATA attribute is in the first 5 attributes.
#pragma clang loop unroll(full)
  for (int i = 0; i < 5; ++i) {
    // Each attributes contain 3 fields: type, length of value, and value.
    struct stun_attribute_header {
      u16 type;
      u16 len;
    } attr_hdr;
    if (bpf_skb_load_bytes_relative(skb, offset, &attr_hdr, sizeof(attr_hdr),
                                    BPF_HDR_START_NET) < 0) {
      return RET_IPTABLES_NOT_MATCHED;
    }
    offset += sizeof(struct stun_attribute_header);
    if (attr_hdr.type == bpf_htons(kDataAttributeType)) {
      return handle_dtls(skb, offset);
    }

    // Move to next attribute.
    offset += bpf_htons(attr_hdr.len);
  }

  return RET_IPTABLES_NOT_MATCHED;
}

// Checks if the given two types represent DTLS version 1.0, which is "254.255".
static __attribute__((always_inline)) int is_dtls_version_1_0(
    const u8 version[2]) {
  return version[0] == 254 && version[1] == 255;
}

// Checks if the given two types represent DTLS version 1.2, which is "254.253".
static __attribute__((always_inline)) int is_dtls_version_1_2(
    const u8 version[2]) {
  return version[0] == 254 && version[1] == 253;
}

static int handle_dtls(struct __sk_buff* skb, u32 base_offset_to_net) {
  u32 offset = base_offset_to_net;

  // Check the first 3 bytes in the payload. If this is a DTLS client hello
  // message we care about, these 3 bytes should be fixed.
  struct {
    u8 type;
    u8 version[2];
  } dtls_hdr;
  if (bpf_skb_load_bytes_relative(skb, offset, &dtls_hdr, sizeof(dtls_hdr),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  // Defined in TLS RFC (RFC 8446 for TLS 1.3).
  const int kTypeHandshakePacket = 22;
  // Version can be either 1.0 or 1.2 for the client hello packet.
  if (dtls_hdr.type != kTypeHandshakePacket ||
      !(is_dtls_version_1_0(dtls_hdr.version) ||
        is_dtls_version_1_2(dtls_hdr.version))) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  // Move to the fragment field, which should contain a Handshake struct.
  const int kFragmentOffsetInDTLSPlainText = 13;
  offset += kFragmentOffsetInDTLSPlainText;

  // Parse the Handshake struct (defined in RFC 6347 Section 4.2.2 for DTLS
  // 1.2) and check if it's a client hello packet.
  u8 msgType;
  if (bpf_skb_load_bytes_relative(skb, offset, &msgType, sizeof(msgType),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  const int kMsgTypeClientHello = 1;
  if (msgType != kMsgTypeClientHello) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  // Move to the body field, which should contain a ClientHello struct.
  const int kBodyOffsetInHandshake = 12;
  offset += kBodyOffsetInHandshake;

  // Parse the ClientHello struct (defined in RFC 6347 Section 4.7.2 for DTLS
  // 1.2). The first 2 bytes should be the version, we only care about 1.2 here
  // now.
  u8 version[2];
  if (bpf_skb_load_bytes_relative(skb, offset, version, sizeof(version),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  if (!is_dtls_version_1_2(version)) {
    return RET_IPTABLES_NOT_MATCHED;
  }

  // In the ClientHello struct, we only care about the extensions field, but
  // there are several fields with variable length before that. We need to get
  // the lengths of these fields and jump over them.

  // Move to session_id field.
  const int kSessionIDOffsetInClientHello = 34;
  offset += kSessionIDOffsetInClientHello;

  // Jump over session_id.
  u8 session_id_size;
  if (bpf_skb_load_bytes_relative(skb, offset, &session_id_size,
                                  sizeof(session_id_size),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  offset += sizeof(session_id_size) + session_id_size;

  // Jump over cookie.
  u8 cookie_size;
  if (bpf_skb_load_bytes_relative(skb, offset, &cookie_size,
                                  sizeof(cookie_size), BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  offset += sizeof(cookie_size) + cookie_size;

  // Jump over cipher_suites.
  u16 cipher_suites_size;
  if (bpf_skb_load_bytes_relative(skb, offset, &cipher_suites_size,
                                  sizeof(cipher_suites_size),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  offset += sizeof(cipher_suites_size) + bpf_htons(cipher_suites_size);

  // Jump over compression_methods.
  u8 compression_methods_size;
  if (bpf_skb_load_bytes_relative(skb, offset, &compression_methods_size,
                                  sizeof(compression_methods_size),
                                  BPF_HDR_START_NET) < 0) {
    return RET_IPTABLES_NOT_MATCHED;
  }
  offset += sizeof(compression_methods_size) + compression_methods_size;

  // We reach the extensions field here. The first two bytes are for its length.
  // We don't need to parse the length so just skip them.
  offset += 2;

  // Parse the extensions field and try to find the "use_srtp" extension in it.
  // Assume that it is in the first 10 extensions.
#pragma clang loop unroll(full)
  for (int i = 0; i < 10; ++i) {
    // See RFC 5246 Section 7.4.1.4 for the definition of struct Extension.
    struct {
      u16 type;
      u16 length;
      // There is data after length but we don't care about it.
    } extension;
    if (bpf_skb_load_bytes_relative(skb, offset, &extension, sizeof(extension),
                                    BPF_HDR_START_NET) < 0) {
      return RET_IPTABLES_NOT_MATCHED;
    }
    const int kExtentionTypeUseSRTP = 14;
    if (extension.type == bpf_htons(kExtentionTypeUseSRTP)) {
      // Found the SRTP extension.
      return RET_IPTABLES_MATCHED;
    }
    offset += sizeof(extension) + bpf_htons(extension.length);
  }

  return RET_IPTABLES_NOT_MATCHED;
}

// A BPF_PROG_TYPE_SOCKET_FILTER type eBPF program attached by iptables with the
// bpf module which checks if the packet is a DTLS client hello packet and
// contains the "use_srtp" extension (which is strong indicator that this
// connection will be used for sending multimedia streams in WebRTC). This
// program is used for detecting if the connection is a WebRTC connection.
//
// See the resources for more information:
// - "WebRTC detection" section in go/cros-wifi-qos-dd for the high-level idea
//   of this program.
// - "bpf" section in `man iptables-extensions` for the (e)BPF usage in
//   iptables.
SEC("socket")
int match_dtls_srtp(struct __sk_buff* skb) {
  if (skb->protocol == bpf_htons(ETH_P_IP)) {
    return handle_ipv4(skb);
  } else if (skb->protocol == bpf_htons(ETH_P_IPV6)) {
    return handle_ipv6(skb);
  }

  return RET_IPTABLES_NOT_MATCHED;
}
