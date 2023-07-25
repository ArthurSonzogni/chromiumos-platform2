// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_PORT_INCLUDE_LINUX_IPV6_H_
#define SECAGENTD_BPF_PORT_INCLUDE_LINUX_IPV6_H_

#include "secagentd/bpf/port/include/linux/skb_buff.h"

#define cros_ipv6_optlen(p) ((BPF_CORE_READ(p, hdrlen) + 1) << 3)
#define cros_ipv6_authlen(p) ((BPF_CORE_READ(p, hdrlen) + 2) << 2)

static inline __attribute__((always_inline)) struct ipv6hdr* cros_ipv6_hdr(
    const struct sk_buff* skb) {
  return (struct ipv6hdr*)cros_skb_network_header(skb);
}

static inline struct ipv6hdr* cros_inner_ipv6_hdr(const struct sk_buff* skb) {
  return (struct ipv6hdr*)cros_skb_inner_network_header(skb);
}

static inline struct ipv6hdr* ipipv6_hdr(const struct sk_buff* skb) {
  return (struct ipv6hdr*)cros_skb_transport_header(skb);
}

#endif  // SECAGENTD_BPF_PORT_INCLUDE_LINUX_IPV6_H_
