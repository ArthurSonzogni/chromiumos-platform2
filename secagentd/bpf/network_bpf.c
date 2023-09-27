// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
#include "include/secagentd/vmlinux/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>

// TODO(b/243453873): Workaround to get code completion working in CrosIDE.
#undef __cplusplus

#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf/bpf_utils.h"
#include "secagentd/bpf/port/include/linux/ipv6.h"
#include "secagentd/bpf/port/include/linux/skb_buff.h"
#include "secagentd/bpf/port/include/net/ipv6.h"
#include "secagentd/bpf/port/include/uapi/asm-generic/errno.h"

const char LICENSE[] SEC("license") = "Dual BSD/GPL";
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, CROS_MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

#define CROS_NF_HOOK_OK (1)

struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, CROS_MAX_SOCKET* CROS_AVG_CONN_PER_SOCKET);
  __type(key, struct cros_flow_map_key);
  __type(value, struct cros_flow_map_value);
} cros_network_flow_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 256);  // 256 devices seems like enough.
  __type(key, int64_t);
  __type(value, int64_t);
} cros_network_external_interfaces SEC(".maps");

/* The process protocol and family information
 * remains the same for a socket for its lifetime.
 * so record it just once.
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, CROS_MAX_SOCKET);
  __type(key, uint64_t);  // A unique ID for a socket.
  __type(value, struct cros_sock_to_process_map_value);
} process_map SEC(".maps");

/* A recording of sockets that have at least one
 * flow map entry associated with it.
 * This should only be used by the BPF to determine
 * if a socket should be added to the socket graveyard
 * at socket release.
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, CROS_MAX_SOCKET);
  __type(key, uint64_t);    // A unique ID for a socket.
  __type(value, uint64_t);  // Also the address of the socket.
} active_socket_map SEC(".maps");

/* BPF Verifier only allows a stack of 512 bytes max.
 * Use this one simple trick that BPF verifiers hate
 * to get around this limitation.
 */
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(key_size, sizeof(uint32_t));
  __uint(value_size, sizeof(struct cros_sock_to_process_map_value));
  __uint(max_entries, 1);
} heap_cros_network_common_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(key_size, sizeof(uint32_t));
  __uint(value_size, sizeof(struct cros_flow_map_value));
  __uint(max_entries, 1);
} heap_cros_flow_map_value SEC(".maps");

// Inspired by net/ipv6/exthdrs_core.c:ipv6_find_hdr
// Return the first non extension next header value
// offset will be set to the first byte following the extended headers.
static inline __attribute__((always_inline)) int
cros_ipv6_get_non_ext_next_header(const struct sk_buff* skb,
                                  unsigned int* offset) {
  unsigned int start = cros_skb_network_offset(skb) + sizeof(struct ipv6hdr);
  __u8 nexthdr = BPF_CORE_READ(cros_ipv6_hdr(skb), nexthdr);
  if (*offset) {
    struct ipv6hdr _ip6, *ip6;
    ip6 = (struct ipv6hdr*)cros_skb_header_pointer(skb, *offset, sizeof(_ip6),
                                                   &_ip6);
    if (!ip6 || (ip6->version != 6)) {
      bpf_printk(
          "ipv6 extended header parsing failed, linear buffer did not contain "
          "all the extended headers.");
      return -CROS_EBADMSG;
    }
    start = *offset + sizeof(struct ipv6hdr);
    nexthdr = ip6->nexthdr;
  }
  int rv = -1;
  int iter = 0;
  // After following 255 headers, give up.
  for (int iter = 0; iter < 255; iter++) {
    struct ipv6_opt_hdr _hdr, *hp;
    unsigned int hdrlen;

    if ((!cros_ipv6_ext_hdr(nexthdr)) || nexthdr == CROS_NEXTHDR_NONE) {
      rv = nexthdr;
      break;
    }
    hp = cros_skb_header_pointer(skb, start, sizeof(_hdr), &_hdr);
    if (!hp)
      return -CROS_EBADMSG;
    if (nexthdr == CROS_NEXTHDR_FRAGMENT) {
      hdrlen = 8;
    } else if (nexthdr == CROS_NEXTHDR_AUTH) {
      hdrlen = cros_ipv6_authlen(hp);
    } else {
      hdrlen = cros_ipv6_optlen(hp);
    }
    nexthdr = BPF_CORE_READ(hp, nexthdr);
    start += hdrlen;
  }
  *offset = start;
  return nexthdr;
}

static int __attribute__((always_inline))
determine_protocol(int family, int socket_type, int protocol) {
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
  } else if (socket_type == CROS_IANA_ICMP6) {
    return CROS_PROTOCOL_ICMP6;
  }
  // The socket type is dgram or stream but the protocol isn't one we recognize.
  return CROS_PROTOCOL_UNKNOWN;
}

static void __attribute__((always_inline))
cros_fill_common(struct cros_network_common* common,
                 const struct socket* sock) {
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  struct sock* sk = BPF_CORE_READ(sock, sk);
  int socket_type = BPF_CORE_READ(sock, type);
  common->family = BPF_CORE_READ(sk, __sk_common).skc_family;
  common->protocol = determine_protocol(common->family, socket_type,
                                        BPF_CORE_READ(sk, sk_protocol));
  cros_fill_task_info(&common->process, t);
}

// Grab the network_device associated with a sk_buff.
// Typically sk_buff.dev points to a valid network_device
// but depending where you are in the network stack it may not yet be populated.
// In that case use the same methodology the kernel uses and derive
// network_device using skb_dst()
static struct net_device* __attribute__((always_inline))
cros_get_net_dev(const struct sk_buff* skb) {
  struct net_device* dev = BPF_CORE_READ(skb, dev);
  if (dev == NULL) {
    dev = BPF_CORE_READ(cros_skb_dst(skb), dev);
  }
  return dev;
}

static bool __attribute__((always_inline))
cros_is_ifindex_external(const struct sk_buff* skb) {
  struct net_device* dev = cros_get_net_dev(skb);
  if (dev == NULL) {
    bpf_printk(
        "Could not determine if device is external. sk_buff contained a null "
        "net_device.");
    // log an error but allow an event to be generated.
    return true;
  }
  int64_t ifindex = BPF_CORE_READ(dev, ifindex);
  if (ifindex < 0) {
    // log an error
    bpf_printk(
        "Could not determine if device is external. ifindex is negative:%d",
        ifindex);
    return false;
  }
  return bpf_map_lookup_elem(&cros_network_external_interfaces, &ifindex) !=
         NULL;
}

static inline __attribute__((always_inline)) int cros_fill_ipv6_5_tuple(
    struct cros_network_5_tuple* five_tuple, const struct sk_buff* skb) {
  const struct ipv6hdr* hdr = (struct ipv6hdr*)(cros_skb_network_header(skb));
  struct sock* sk = BPF_CORE_READ(skb, sk);
  BPF_CORE_READ_INTO(&(five_tuple->source_addr.addr6), hdr,
                     saddr.in6_u.u6_addr8);
  five_tuple->family = CROS_FAMILY_AF_INET6;
  BPF_CORE_READ_INTO(&(five_tuple->dest_addr.addr6), hdr, daddr.in6_u.u6_addr8);
  int packet_size = bpf_ntohs(BPF_CORE_READ(hdr, payload_len));
  int protocol = 0;
  unsigned int transport_offset = 0;
  int next_header = cros_ipv6_get_non_ext_next_header(skb, &transport_offset);
  switch (next_header) {
    case CROS_NEXTHDR_ICMP:
      five_tuple->protocol = CROS_PROTOCOL_ICMP;
      break;
    case CROS_NEXTHDR_TCP:
      five_tuple->protocol = CROS_PROTOCOL_TCP;
      const struct tcphdr* tcp_header =
          (const struct tcphdr*)cros_skb_transport_header(skb);
      five_tuple->source_port = bpf_ntohs(BPF_CORE_READ(tcp_header, source));
      five_tuple->dest_port = bpf_ntohs(BPF_CORE_READ(tcp_header, dest));
      break;
    case CROS_NEXTHDR_UDP:
      five_tuple->protocol = CROS_PROTOCOL_UDP;
      const struct udphdr* udp_header =
          (const struct udphdr*)cros_skb_transport_header(skb);
      five_tuple->source_port = bpf_ntohs(BPF_CORE_READ(udp_header, source));
      five_tuple->dest_port = bpf_ntohs(BPF_CORE_READ(udp_header, dest));
      break;
    case -CROS_EBADMSG:
      five_tuple->protocol = CROS_PROTOCOL_UNKNOWN;
      break;
    default:
      // IPv6 header does not have a next header value reserved for raw sockets.
      // Use the socket type to determine if this is a raw socket.
      if (BPF_CORE_READ(sk, sk_type) == SOCK_RAW) {
        five_tuple->protocol = CROS_PROTOCOL_RAW;
      } else {
        five_tuple->protocol = CROS_PROTOCOL_UNKNOWN;
      }

      break;
  }
  return packet_size;
}

static inline __attribute__((always_inline)) int cros_fill_ipv4_5_tuple(
    struct cros_network_5_tuple* five_tuple, const struct sk_buff* skb) {
  const struct iphdr* hdr = (struct iphdr*)(cros_skb_network_header(skb));
  int protocol = BPF_CORE_READ(hdr, protocol);
  five_tuple->family = CROS_FAMILY_AF_INET;
  five_tuple->source_addr.addr4 = BPF_CORE_READ(hdr, saddr);
  five_tuple->dest_addr.addr4 = BPF_CORE_READ(hdr, daddr);
  int packet_size = bpf_ntohs(BPF_CORE_READ(hdr, tot_len));

  switch (protocol) {
    case IPPROTO_ICMP:
      five_tuple->protocol = CROS_PROTOCOL_ICMP;
      break;
    case IPPROTO_RAW:
      five_tuple->protocol = CROS_PROTOCOL_RAW;
      break;
    case IPPROTO_UDP:
      five_tuple->protocol = CROS_PROTOCOL_UDP;
      const struct udphdr* udp_header =
          (const struct udphdr*)cros_skb_transport_header(skb);
      five_tuple->dest_port = bpf_ntohs(BPF_CORE_READ(udp_header, dest));
      five_tuple->source_port = bpf_ntohs(BPF_CORE_READ(udp_header, source));
      break;
    case IPPROTO_TCP:
      five_tuple->protocol = CROS_PROTOCOL_TCP;
      const struct tcphdr* tcp_header =
          (const struct tcphdr*)cros_skb_transport_header(skb);
      five_tuple->source_port = bpf_ntohs(BPF_CORE_READ(tcp_header, source));
      five_tuple->dest_port = bpf_ntohs(BPF_CORE_READ(tcp_header, dest));
      break;
    default:
      five_tuple->protocol = CROS_PROTOCOL_UNKNOWN;
      break;
  }
  return packet_size;
}

static inline __attribute__((always_inline)) void cros_fill_5_tuple_from_sock(
    struct cros_network_5_tuple* five_tuple, const struct socket* sock) {
  struct sock_common sk_common = BPF_CORE_READ(sock, sk, __sk_common);
  five_tuple->family = sk_common.skc_family;
  five_tuple->protocol =
      determine_protocol(five_tuple->family, BPF_CORE_READ(sock, type),
                         BPF_CORE_READ(sock, sk, sk_protocol));
  if (five_tuple->family == CROS_FAMILY_AF_INET) {
    five_tuple->dest_addr.addr4 = sk_common.skc_daddr;
    five_tuple->source_addr.addr4 = sk_common.skc_rcv_saddr;
  } else if (five_tuple->family == CROS_FAMILY_AF_INET6) {
    __builtin_memcpy(five_tuple->source_addr.addr6,
                     sk_common.skc_v6_rcv_saddr.in6_u.u6_addr8,
                     sizeof(five_tuple->source_addr.addr6));
    __builtin_memcpy(five_tuple->dest_addr.addr6,
                     sk_common.skc_v6_daddr.in6_u.u6_addr8,
                     sizeof(five_tuple->dest_addr.addr6));
  }
  five_tuple->dest_port = sk_common.skc_dport;
  five_tuple->source_port = sk_common.skc_num;
}

static inline __attribute__((always_inline)) bool cros_socket_is_monitored(
    struct sock* sk) {
  uint64_t sock_key = (uint64_t)(BPF_PROBE_READ(sk, sk_socket));
  return bpf_map_lookup_elem(&active_socket_map, &sock_key) != NULL;
}

static inline __attribute__((always_inline)) int create_process_map_entry(
    struct socket* sock, const char* ctx) {
  struct cros_sock_to_process_map_value* process_value;
  const uint32_t zero = 0;
  uint64_t key = (uint64_t)sock;
  process_value = bpf_map_lookup_elem(&heap_cros_network_common_map, &zero);
  if (process_value == NULL) {
    bpf_printk(
        "ERROR: Failed to allocate a cros_sock_to_process_map_value off a map "
        "backed heap for %s",
        ctx);
    return -1;
  }
  __builtin_memset(process_value, 0,
                   sizeof(struct cros_sock_to_process_map_value));
  cros_fill_common(&process_value->common, sock);
  process_value->garbage_collect_me = false;
  bpf_map_update_elem(&process_map, &sock, process_value, BPF_NOEXIST);
  return 0;
}

static inline __attribute__((always_inline)) int cros_maybe_new_socket(
    struct socket* sock) {
  uint64_t sock_key = (uint64_t)(sock);
  uint32_t zero = 0;
  if (bpf_map_update_elem(&active_socket_map, &sock_key, &sock_key,
                          BPF_NOEXIST) != 0) {
    return -1;
  }
  return 0;
}

static inline __attribute__((always_inline)) void cros_new_flow_entry(
    struct cros_flow_map_key* key_ref,
    enum cros_network_socket_direction direction,
    uint32_t tx_bytes,
    uint32_t rx_bytes) {
  struct cros_flow_map_value* value;
  // Use the process value from the heap.
  const uint32_t zero = 0;
  value = bpf_map_lookup_elem(&heap_cros_flow_map_value, &zero);
  if (value == NULL) {
    // This is a pretty fatal error so add a return value for
    // cros_new_flow_entry
    bpf_printk("ERROR: Unable to map-allocate a flow map value for creation.");
    return;
  }
  value->garbage_collect_me = false;
  value->direction = direction;
  value->rx_bytes = rx_bytes;
  value->tx_bytes = tx_bytes;
  uint64_t sock = key_ref->sock;
  struct cros_sock_to_process_map_value* process_value =
      bpf_map_lookup_elem(&process_map, &sock);
  if (process_value != NULL) {
    __builtin_memcpy(&value->task_info, &process_value->common.process,
                     sizeof(value->task_info));
    if (bpf_map_update_elem(&cros_network_flow_map, key_ref, value,
                            BPF_NOEXIST) < 0) {
      bpf_printk("WARNING: Could not create flow entry: Entry already exists.");
    }
  }
}

static inline __attribute__((always_inline)) int cros_handle_tx_rx(
    const struct sk_buff* skb,
    bool is_ipv6,
    bool is_tx,
    const char* calling_func_name) {
  struct net_device* dev = cros_get_net_dev(skb);
  if (!cros_is_ifindex_external(skb)) {
    return -1;
  }
  struct sock* sk = BPF_CORE_READ(skb, sk);
  struct socket* sock = BPF_CORE_READ(skb, sk, sk_socket);
  if (sock == NULL) {
    // don't care about flows that aren't associated with sockets.
    return -1;
  }
  cros_maybe_new_socket(sock);
  struct cros_flow_map_value* value_ref;
  struct cros_flow_map_key key;
  // Fun fact: BPF verifier will complain if the key contains
  // any uninitialized values.
  __builtin_memset(&key, 0, sizeof(key));
  key.sock = (uint64_t)BPF_CORE_READ(sk, sk_socket);
  int bytes = 0;

  if (is_ipv6) {
    bytes = cros_fill_ipv6_5_tuple(&key.five_tuple, skb);
  } else {
    bytes = cros_fill_ipv4_5_tuple(&key.five_tuple, skb);
  }

  value_ref = bpf_map_lookup_elem(&cros_network_flow_map, &key);
  if (value_ref) {  // entry already exists.
    if (is_tx) {
      value_ref->tx_bytes = value_ref->tx_bytes + bytes;
    } else {
      value_ref->rx_bytes = value_ref->rx_bytes + bytes;
    }
  } else {
    // The socket was likely in operation before this BPF program was loaded
    // so we can't be sure of the direction.
    enum cros_network_socket_direction dir = CROS_SOCKET_DIRECTION_UNKNOWN;

    if (is_tx) {
      cros_new_flow_entry(&key, /*direction*/ dir,
                          /*tx_bytes*/ bytes, /*rx_bytes*/ 0);

    } else {
      cros_new_flow_entry(&key, /*direction*/ dir,
                          /*tx_bytes*/ 0, /*rx_bytes*/ bytes);
    }
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/inet_listen",
                      "raw_tracepoint/cros_inet_listen_exit")
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
  __builtin_memcpy(
      sl->ipv6_addr,
      BPF_CORE_READ(sk, __sk_common).skc_v6_rcv_saddr.in6_u.u6_addr8,
      sizeof(sl->ipv6_addr) / sizeof(uint8_t));
  bpf_ringbuf_submit(event, 0);
  return 0;
}

// ipv4 receive path. Right before userspace receives the packet.
CROS_IF_FUNCTION_HOOK("fexit/ip_protocol_deliver_rcu",
                      "raw_tracepoint/cros_ip_protocol_deliver_rcu_enter")
int BPF_PROG(cros_handle_ip_protocol_deliver_rcu_enter,
             struct net* net,
             struct sk_buff* skb,
             int protocol) {
  return cros_handle_tx_rx(/* skb */ skb,
                           /* is_ipv6 */ false,
                           /* is_tx */ false, "ip_protocol_deliver_rcu");
}

// IPv4 transmit path.
CROS_IF_FUNCTION_HOOK("fexit/__ip_local_out",
                      "raw_tracepoint/cros__ip_local_out_exit")
int BPF_PROG(cros_handle__ip_local_out_exit,
             struct net* net,
             struct sock* sk,
             struct sk_buff* skb,
             int rv) {
  return cros_handle_tx_rx(/* skb */ skb,
                           /* is_ipv6 */ false,
                           /* is_tx */ true, "__ip_local_out");
}

// IPv6 transmit path. Fairly close to where a packet gets handed off
// to the device layer.
CROS_IF_FUNCTION_HOOK("fenter/ip6_finish_output2",
                      "raw_tracepoint/cros_ip6_finish_output2_enter")
int BPF_PROG(cros_handle_ip6_finish_output2_enter,
             struct net* net,
             struct sock* sk,
             struct sk_buff* skb) {
  return cros_handle_tx_rx(/* skb */ skb,
                           /* is_ipv6 */ true,
                           /* is_tx */ true, "ip6_finish_output2");
}

CROS_IF_FUNCTION_HOOK("fexit/ip6_input_finish",
                      "raw_tracepoint/cros_ip6_input_finish_enter")
int BPF_PROG(cros_handle_ip6_input_exit,
             struct net* net,
             struct sock* sk,
             struct sk_buff* skb) {
  return cros_handle_tx_rx(/* skb */ skb,
                           /* is_ipv6 */ true,
                           /* is_tx */ false, "ip6_input_exit");
}

CROS_IF_FUNCTION_HOOK("fexit/inet_accept",
                      "raw_tracepoint/cros_inet_accept_exit")
int BPF_PROG(cros_handle_inet_accept_exit,
             struct socket* sock,
             struct socket* newsock,
             int flags,
             bool kern,
             int rv) {
  if (rv < 0) {
    return 0;
  }
  struct cros_flow_map_value* value_ref;
  struct cros_flow_map_key key;
  // Fun fact: BPF verifier will complain if the key contains
  // any uninitialized values.
  __builtin_memset(&key, 0, sizeof(key));
  key.sock = (uint64_t)newsock;
  cros_maybe_new_socket(newsock);
  cros_fill_5_tuple_from_sock(&key.five_tuple, newsock);
  // new socket, new process entry.
  if (create_process_map_entry(newsock, "inet_accept") < 0) {
    bpf_printk("ERROR: inet_accept unable to capture process context.");
    return 0;
  }
  value_ref = bpf_map_lookup_elem(&cros_network_flow_map, &key);
  if (value_ref) {  // entry already exist.. this shouldn't be the case.
    bpf_printk(
        "cros_network_accept_exit encountered a socket with existing flow "
        "entry");
  } else {  // entry does not exist so must be an outbound connection.
    cros_new_flow_entry(&key, /*direction*/ CROS_SOCKET_DIRECTION_IN,
                        /*tx_bytes*/ 0, /*rx_bytes*/ 0);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/inet_stream_connect",
                      "raw_tracepoint/cros_inet_stream_connect_exit")
int BPF_PROG(cros_handle_inet_stream_connect_exit,
             struct socket* sock,
             struct sockaddr* uaddr,
             int addr_lens,
             int flags,
             int is_sendmsg,
             int rv) {
  if (rv < 0) {
    return 0;
  }
  struct cros_flow_map_value* value_ref;
  struct cros_flow_map_key key;
  // Fun fact: BPF verifier will complain if the key contains
  // any uninitialized values.
  __builtin_memset(&key, 0, sizeof(key));
  key.sock = (uint64_t)sock;
  cros_maybe_new_socket(sock);
  cros_fill_5_tuple_from_sock(&key.five_tuple, sock);
  if (create_process_map_entry(sock, "inet_accept") < 0) {
    bpf_printk("ERROR: inet_accept unable to record process context.");
    return 0;
  }
  value_ref = bpf_map_lookup_elem(&cros_network_flow_map, &key);
  if (value_ref) {
    value_ref->direction = CROS_SOCKET_DIRECTION_OUT;
  } else {  // entry does not exist so must be an outbound connection.
    cros_new_flow_entry(&key, /*direction*/ CROS_SOCKET_DIRECTION_OUT,
                        /*tx_bytes*/ 0, /*rx_bytes*/ 0);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/inet_release",
                      "raw_tracepoint/cros_inet_release_enter")
int BPF_PROG(cros_handle_inet_release_enter, struct socket* sock) {
  uint64_t key = (uint64_t)sock;
  if (bpf_map_delete_elem(&active_socket_map, &key) == -1) {
    bpf_printk("inet_release: active socket deletion failed for %d.", key);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fenter/inet_sendmsg",
                      "raw_tracepoint/cros_inet_sendmsg_enter")
int BPF_PROG(cros_handle_inet_sendmsg_enter,
             struct socket* sock,
             struct msghdr* msg,
             size_t size) {
  // This is a safe area to grab process context.
  struct sock* sk = BPF_CORE_READ(sock, sk);
  if (create_process_map_entry(sock, "inet_sendmsg") < 0) {
    bpf_printk("ERROR: inet_sendmsg unable to capture process context.");
    return 0;
  }
  cros_maybe_new_socket(sock);
  return 0;
}

CROS_IF_FUNCTION_HOOK("fenter/inet_recvmsg",
                      "raw_tracepoint/cros_inet_recvmsg_exit")
int BPF_PROG(cros_handle_inet_recvmsg_exit,
             struct socket* sock,
             struct msghdr* msg,
             size_t size,
             int flags) {
  // This is a safe area to grab process context.
  if (create_process_map_entry(sock, "inet_recvmsg") < 0) {
    bpf_printk("ERROR: inet_recvmsg unable to capture process context.");
  }
  cros_maybe_new_socket(sock);
  return 0;
}
