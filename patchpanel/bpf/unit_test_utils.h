// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_BPF_UNIT_TEST_UTILS_H_
#define PATCHPANEL_BPF_UNIT_TEST_UTILS_H_

// This header will be included by both the eBPF source code (in C) and unit
// test source code (in C++). For the eBPF source code, we include this file
// instead of the headers from eBPF so that we can fake some BPF APIs / structs
// to make it testable in unit tests.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mute some macros used by BPF APIs which can be ignored in unit tests.
#define SEC(name)
#define BPF_HDR_START_NET (0)

#define u32 uint32_t
#define u16 uint16_t
#define u8 uint8_t

// We only need a few fields in sk_buff.
struct __sk_buff {
  uint16_t protocol;
  u8* data;
  u32 len;
};

// Copy `len` bytes from `skb->data + offset` to `to`. `start_header` doesn't
// matter in the fake implementation (it should always be BPF_HDR_START_NET in
// our BPF code).
int bpf_skb_load_bytes_relative(
    struct __sk_buff* skb, u32 offset, void* to, size_t len, u32 start_header);

u16 bpf_htons(u16 a);

#ifdef __cplusplus
}
#endif

#endif  // PATCHPANEL_BPF_UNIT_TEST_UTILS_H_
