// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h at first to declare all kernel types.
#include "include/patchpanel/vmlinux/vmlinux.h"

#include <bpf/bpf_helpers.h>

// The return values used in this program. In the context of iptables, a
// non-zero return value means a match.
#define RET_IPTABLES_MATCHED (1)
#define RET_IPTABLES_NOT_MATCHED (0)

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

// A BPF_PROG_TYPE_SOCKET_FILTER type eBPF program attached by iptables with the
// bpf module which checks if the packet is a DTLS client hello packet and
// contains the "use_srtp" extension. This program is used for detecting if the
// connection is a WebRTC connection.
//
// See the resources for more information:
// - "WebRTC detection" section in go/cros-wifi-qos-dd for the high-level idea
//   of this program.
// - "bpf" section in `man iptables-extensions` for the (e)BPF usage in
//   iptables.
SEC("socket")
int match_dtls_srtp(struct __sk_buff* skb) {
  // TODO(b/296952085): Add implementation.
  return RET_IPTABLES_NOT_MATCHED;
}
