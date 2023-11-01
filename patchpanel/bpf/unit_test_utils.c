// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/bpf/unit_test_utils.h"

#include <string.h>

#include <arpa/inet.h>

int bpf_skb_load_bytes_relative(
    struct __sk_buff* skb, u32 offset, void* to, size_t len, u32 start_header) {
  if (offset + len > skb->len) {
    return -1;
  }
  memcpy(to, skb->data + offset, len);
  return 0;
}

u16 bpf_htons(u16 a) {
  return htons(a);
}
