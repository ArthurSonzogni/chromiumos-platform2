// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// bpf_helpers.h uses types defined here
#include "include/vm_tools/port_listener/vmlinux/vmlinux.h"

#include <bpf/bpf_helpers.h>

#include "vm_tools/port_listener/common.h"

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 24);
} events SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __type(key, struct sock*);
  __type(value, __u8);
  __uint(max_entries, 65535);
  __uint(map_flags, BPF_F_NO_PREALLOC);
} sockmap SEC(".maps");

const __u8 set_value = 0;

SEC("tp/sock/inet_sock_set_state")
int tracepoint_inet_sock_set_state(
    struct trace_event_raw_inet_sock_set_state* ctx) {
  // We don't support anything other than TCP.
  if (ctx->protocol != IPPROTO_TCP) {
    return 0;
  }
  struct sock* sk = (struct sock*)ctx->skaddr;
  // If we're transitioning away from LISTEN but we don't know about this
  // socket yet then don't report anything.
  if (ctx->oldstate == BPF_TCP_LISTEN &&
      bpf_map_lookup_elem(&sockmap, &sk) == NULL) {
    return 0;
  }
  // If we aren't transitioning to or from TCP_LISTEN then we don't care.
  if (ctx->newstate != BPF_TCP_LISTEN && ctx->oldstate != BPF_TCP_LISTEN) {
    return 0;
  }

  struct event* ev;
  ev = bpf_ringbuf_reserve(&events, sizeof(*ev), 0);
  if (!ev) {
    return 0;
  }
  ev->port = ctx->sport;

  if (ctx->newstate == BPF_TCP_LISTEN) {
    bpf_map_update_elem(&sockmap, &sk, &set_value, BPF_ANY);
    ev->state = kPortListenerUp;
  }
  if (ctx->oldstate == BPF_TCP_LISTEN) {
    bpf_map_delete_elem(&sockmap, &sk);
    ev->state = kPortListenerDown;
  }
  bpf_ringbuf_submit(ev, 0);

  return 0;
}
