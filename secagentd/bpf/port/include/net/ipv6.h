// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_PORT_INCLUDE_NET_IPV6_H_
#define SECAGENTD_BPF_PORT_INCLUDE_NET_IPV6_H_

/*
 *    NextHeader field of IPv6 header
 */

#define CROS_NEXTHDR_HOP 0        /* Hop-by-hop option header. */
#define CROS_NEXTHDR_IPV4 4       /* IPv4 in IPv6 */
#define CROS_NEXTHDR_TCP 6        /* TCP segment. */
#define CROS_NEXTHDR_UDP 17       /* UDP message. */
#define CROS_NEXTHDR_IPV6 41      /* IPv6 in IPv6 */
#define CROS_NEXTHDR_ROUTING 43   /* Routing header. */
#define CROS_NEXTHDR_FRAGMENT 44  /* Fragmentation/reassembly header. */
#define CROS_NEXTHDR_GRE 47       /* GRE header. */
#define CROS_NEXTHDR_ESP 50       /* Encapsulating security payload. */
#define CROS_NEXTHDR_AUTH 51      /* Authentication header. */
#define CROS_NEXTHDR_ICMP 58      /* ICMP for IPv6. */
#define CROS_NEXTHDR_NONE 59      /* No next header */
#define CROS_NEXTHDR_DEST 60      /* Destination options header. */
#define CROS_NEXTHDR_SCTP 132     /* SCTP message. */
#define CROS_NEXTHDR_MOBILITY 135 /* Mobility header. */

static inline __attribute__((always_inline)) bool cros_ipv6_ext_hdr(
    unsigned char nexthdr) {
  return (nexthdr == CROS_NEXTHDR_HOP) || (nexthdr == CROS_NEXTHDR_ROUTING) ||
         (nexthdr == CROS_NEXTHDR_FRAGMENT) || (nexthdr == CROS_NEXTHDR_AUTH) ||
         (nexthdr == CROS_NEXTHDR_NONE) || (nexthdr == CROS_NEXTHDR_DEST);
}

#endif  // SECAGENTD_BPF_PORT_INCLUDE_NET_IPV6_H_
