// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Inspired by /include/linux/skbuff.h:skb_transport_header

#ifndef SECAGENTD_BPF_PORT_INCLUDE_LINUX_SKB_BUFF_H_
#define SECAGENTD_BPF_PORT_INCLUDE_LINUX_SKB_BUFF_H_
#include "include/secagentd/vmlinux/vmlinux.h"

#include <bpf/bpf_helpers.h>

#define CROS_SKB_DST_NOREF 1UL
#define CROS_SKB_DST_PTRMASK ~(CROS_SKB_DST_NOREF)

static inline __attribute__((always_inline)) struct dst_entry* cros_skb_dst(
    const struct sk_buff* skb) {
  return (struct dst_entry*)(BPF_CORE_READ(skb, _skb_refdst) &
                             CROS_SKB_DST_PTRMASK);
}

static inline __attribute__((always_inline)) unsigned char*
cros_skb_network_header(const struct sk_buff* skb) {
  return BPF_CORE_READ(skb, head) + BPF_CORE_READ(skb, network_header);
}

static inline __attribute__((always_inline)) unsigned char*
cros_skb_transport_header(const struct sk_buff* skb) {
  return BPF_CORE_READ(skb, head) + BPF_CORE_READ(skb, transport_header);
}

static inline __attribute__((always_inline)) unsigned char*
cros_skb_inner_network_header(const struct sk_buff* skb) {
  return skb->head + skb->inner_network_header;
}

static inline __attribute__((always_inline)) int cros_skb_network_offset(
    const struct sk_buff* skb) {
  return cros_skb_network_header(skb) - BPF_CORE_READ(skb, data);
}

static inline __attribute__((always_inline)) bool
cros_skb_transport_header_was_set(const struct sk_buff* skb) {
  return BPF_CORE_READ(skb, transport_header) !=
         (typeof(skb->transport_header))~0U;
}

static inline __attribute__((always_inline)) unsigned int cros_skb_headlen(
    const struct sk_buff* skb) {
  return BPF_CORE_READ(skb, len) - BPF_CORE_READ(skb, data_len);
}

static inline __attribute__((always_inline)) void* cros__skb_header_pointer(
    const struct sk_buff* skb,
    int offset,
    int len,
    const void* data,
    int hlen,
    void* buffer) {
  if (hlen - offset >= len) {
    return (void*)data + offset;  // NOLINT
  }
  // For reasons unknown to me, bpf_skb_load_bytes is not being recognized
  // as a proper bpf helper...so if the field we want in the IP or transport
  // header is not in linear memory then we're out of luck. if (!skb ||
  // bpf_skb_load_bytes(skb, offset, buffer, len) < 0) {
  if (!skb) {
    return NULL;
  }
  return buffer;
}

static inline __attribute__((always_inline)) void* cros_skb_header_pointer(
    const struct sk_buff* skb, int offset, int len, void* buffer) {
  return cros__skb_header_pointer(skb, offset, len, BPF_CORE_READ(skb, data),
                                  cros_skb_headlen(skb), buffer);
}

#endif  // SECAGENTD_BPF_PORT_INCLUDE_LINUX_SKB_BUFF_H_
