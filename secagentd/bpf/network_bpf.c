// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
#include "include/secagentd/vmlinux/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// TODO(b/243453873): Workaround to get code completion working in CrosIDE.
#undef __cplusplus
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf/bpf_utils.h"

const char LICENSE[] SEC("license") = "Dual BSD/GPL";
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, CROS_MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

#define CROS_IANA_HOPOPT (0)
#define CROS_IANA_ICMP (1)
#define CROS_IANA_TCP (6)
#define CROS_IANA_UDP (17)

static int __attribute__((always_inline))
determine_protocol(int family, int socket_type, int protocol) {
  // From /etc/protocols, see also
  // http://www.iana.org/assignments/protocol-numbers
  if (protocol == CROS_IANA_HOPOPT) {  // generic ip protocol.
    // Determine protocol based on domain(family) and type.
    // SOCK_STREAM protocols under AF_INET , AF_INET6 are TCP/IP
    // SOCK_DGRAM protocols under AF_INET, AF_INET6 are UDP
    if (family == CROS_FAMILY_AF_INET || family == CROS_FAMILY_AF_INET6) {
      if (socket_type == SOCK_STREAM) {
        return CROS_PROTOCOL_TCP;
      } else if (socket_type == SOCK_DGRAM) {
        return CROS_PROTOCOL_UDP;
      } else if (socket_type == SOCK_RAW) {
        return CROS_PROTOCOL_RAW;
      }
    }
  } else if (protocol == CROS_IANA_ICMP) {
    return CROS_PROTOCOL_ICMP;
  } else if (protocol == CROS_IANA_TCP) {
    return CROS_PROTOCOL_TCP;
  } else if (protocol == CROS_IANA_UDP) {
    return CROS_PROTOCOL_UDP;
  } else if (socket_type == SOCK_RAW) {
    return CROS_PROTOCOL_RAW;
  }
  // The socket type is dgram or stream but the protocol isn't one we recognize.
  return CROS_PROTOCOL_UNKNOWN;
}

static void __attribute__((always_inline))
cros_fill_common(struct cros_network_common* common, struct socket* s) {
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  struct sock* sk = BPF_CORE_READ(s, sk);
  int socket_type = BPF_CORE_READ(s, type);
  common->family = BPF_CORE_READ(sk, __sk_common).skc_family;
  common->protocol = determine_protocol(common->family, socket_type,
                                        BPF_CORE_READ(sk, sk_protocol));
  common->dev_if = BPF_CORE_READ(sk, __sk_common).skc_bound_dev_if;
  cros_fill_task_info(&common->process, t);
}

CROS_IF_FUNCTION_HOOK("fexit/inet_listen", "raw_tracepoint/cros_inet_listen")
int BPF_PROG(cros_handle_inet_listen,
             struct socket* socket,
             int backlog,
             int rv) {
  if (rv != 0) {
    return 0;
  }
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  if (is_kthread(t)) {
    return 0;
  }
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    return 0;
  }
  struct sock* sk = BPF_CORE_READ(socket, sk);
  event->type = kNetworkEvent;
  event->data.network_event.type = kNetworkSocketListen;
  struct cros_network_socket_listen* sl =
      &(event->data.network_event.data.socket_listen);
  cros_fill_common(&sl->common, socket);
  sl->socket_type = BPF_CORE_READ(socket, type);
  // Extract out the source port.
  sl->port = BPF_CORE_READ(sk, __sk_common).skc_num;
  // Fill out the IPv4 address.
  sl->ipv4_addr = BPF_CORE_READ(sk, __sk_common).skc_rcv_saddr;
  __builtin_memmove(
      sl->ipv6_addr,
      BPF_CORE_READ(sk, __sk_common).skc_v6_rcv_saddr.in6_u.u6_addr8,
      sizeof(sl->ipv6_addr) / sizeof(uint8_t));
  bpf_ringbuf_submit(event, 0);
  return 0;
}
