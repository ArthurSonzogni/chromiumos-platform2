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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define CROS_FULL_SK_STORAGE_SUPPORT
#define CROS_FULL_GET_SOCKET_COOKIE_SUPPORT
#else
#undef CROS_FULL_SK_STORAGE_SUPPORT
#undef CROS_FULL_GET_SOCKET_COOKIE_SUPPORT
#endif

struct cros_sk_info {
  enum cros_network_family family;
  enum cros_network_protocol protocol;
  struct cros_process_start process_start;
  // include a flow_map_value to drastically reduce stack usage.
  struct cros_flow_map_value flow_map_value_scratchpad;
  uint64_t sock_id;
  bool has_full_process_info;
} __attribute__((aligned(8)));

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

#define CROS_SOCK_INVALID (2)
#define CROS_IF_NOT_EXTERNAL (3)
#define CROS_FAILED_HEAP_ALLOC (4)
#define CROS_NO_SOCK_TO_PROCESS (5)
#define CROS_NO_SK_STORAGE_ALLOCATED (6)
#define CROS_UNSUPPORTED_IP_VERSION (7)
#define CROS_NO_NEW_FLOW_ON_RX (8)
#define CROS_BAD_PARAMETER (9)
#define CROS_OTHER_ERROR (10)

#ifdef CROS_FULL_SK_STORAGE_SUPPORT
struct {
  __uint(type, BPF_MAP_TYPE_SK_STORAGE);
  __uint(map_flags, BPF_F_NO_PREALLOC);
  __type(key, int);
  __type(value, struct cros_sk_info);
} sk_storage SEC(".maps");

static inline __attribute__((always_inline)) int cros_get_socket_id(
    struct sock* sk, uint64_t* id) {
  if (!sk) {
    bpf_printk("cros_sk_socket_id failed, sk is NULL");
    return -CROS_BAD_PARAMETER;
  }
  if (!id) {
    return -CROS_BAD_PARAMETER;
  }
  *id = bpf_get_socket_cookie(sk);
  return 0;
}

static inline __attribute__((always_inline)) struct cros_sk_info*
cros_sk_storage_get_mutable(struct sock* sk) {
  if (!sk) {
    bpf_printk("cros_sk_storage_get_mutable failed, sk is NULL");
    return (struct cros_sk_info*)NULL;
  }
  struct cros_sk_info* sk_info =
      (struct cros_sk_info*)bpf_sk_storage_get(&sk_storage, sk, 0, 0);
  return sk_info;
}

static inline __attribute__((always_inline)) const struct cros_sk_info*
cros_sk_storage_get(struct sock* sk) {
  return cros_sk_storage_get_mutable(sk);
}

static inline __attribute__((always_inline)) struct cros_sk_info*
cros_sk_storage_get_or_create(struct sock* sk) {
  if (!sk) {
    bpf_printk("cros_sk_storage_get_or_create failed, sk is NULL");
    return (struct cros_sk_info*)NULL;
  }
  return (struct cros_sk_info*)bpf_sk_storage_get(&sk_storage, sk, 0,
                                                  BPF_SK_STORAGE_GET_F_CREATE);
}

static inline __attribute__((always_inline)) int cros_sk_storage_set(
    const struct cros_sk_info* sk_info, const struct sock* sk) {
  if (!sk) {
    bpf_printk("cros_sk_storage_set failed, sk is NULL");
    return -CROS_BAD_PARAMETER;
  }
  // storage mutation does not require an update call for sk_storage.
  return 0;
}
#else
/* BPF Verifier only allows a stack of 512 bytes max.
 * Use this one simple trick that BPF verifiers hate
 * to get around this limitation.
 */
struct {
  // kernel v5.10 does not properly support sk_storage and get socket cookie
  // support.
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(key_size, sizeof(uint32_t));
  __uint(value_size, sizeof(struct cros_sk_info));
  __uint(max_entries, 1);
} heap_cros_sk_info SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, CROS_MAX_SOCKET);
  __type(key, uint64_t);
  __type(value, struct cros_sk_info);
} sk_addr_storage SEC(".maps");

static inline __attribute__((always_inline)) int cros_get_socket_id(
    struct sock* sk, uint64_t* id) {
  if (!sk) {
    bpf_printk("cros_get_socket_id failed, sk is NULL");
    return CROS_BAD_PARAMETER;
  }
  if (!id) {
    bpf_printk("cros_get_socket_id failed, id is NULL");
    return CROS_BAD_PARAMETER;
  }
  *id = (uint64_t)sk;
  return 0;
}

static inline __attribute__((always_inline)) struct cros_sk_info*
cros_sk_storage_get_mutable(struct sock* sk) {
  uint64_t key = (uint64_t)sk;
  struct cros_sk_info* sk_info = bpf_map_lookup_elem(&sk_addr_storage, &key);
  return sk_info;
}

static inline __attribute__((always_inline)) const struct cros_sk_info*
cros_sk_storage_get(struct sock* sk) {
  return cros_sk_storage_get_mutable(sk);
}

static inline __attribute__((always_inline)) struct cros_sk_info*
cros_sk_storage_get_or_create(struct sock* sk) {
  uint64_t key = (uint64_t)sk;
  const uint32_t zero = 0;
  struct cros_sk_info* sk_info = bpf_map_lookup_elem(&sk_addr_storage, &key);
  if (sk_info) {
    return sk_info;
  }
  return bpf_map_lookup_elem(&heap_cros_sk_info, &zero);
}

static inline __attribute__((always_inline)) int cros_sk_storage_set(
    const struct cros_sk_info* sk_info, const struct sock* sk) {
  uint64_t key = (uint64_t)sk;
  bpf_map_update_elem(&sk_addr_storage, &key, sk_info, BPF_NOEXIST);
  return 0;
}
#endif

// Populated by process exec bpf program and reused by network events bpf
// to achieve full process context.
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 65536);  // up to 2^16 task info for processes can be
  // stored.
  __type(key, uint32_t);
  __type(value, struct cros_process_start);
  __uint(map_flags, BPF_F_NO_PREALLOC);
  __uint(pinning, LIBBPF_PIN_BY_NAME);  // map will be shared across bpf objs.
} shared_process_info SEC(".maps");

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
    return true;
  }
  int64_t ifindex = BPF_CORE_READ(dev, ifindex);
  if (ifindex < 0) {
    bpf_printk(
        "Could not determine if device is external. ifindex is negative:%d",
        ifindex);
    return false;
  }

  bool is_init_netns = false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
  is_init_netns = BPF_CORE_READ(dev, nd_net.net, net_cookie) == 1;
#else
  is_init_netns = BPF_CORE_READ(dev, nd_net.net, net_cookie.counter) == 1;
#endif
  if (!is_init_netns) {
    return false;
  }
  return (bpf_map_lookup_elem(&cros_network_external_interfaces, &ifindex) !=
          NULL);
}

static inline __attribute__((always_inline)) int cros_fill_ipv6_5_tuple(
    struct cros_network_5_tuple* five_tuple,
    const struct sk_buff* skb,
    bool isTx) {
  const struct ipv6hdr* hdr = (struct ipv6hdr*)(cros_skb_network_header(skb));
  struct sock* sk = BPF_CORE_READ(skb, sk);

  uint16_t* dest_port;    // write dest port from protocol header to here.
  uint16_t* source_port;  // write source port from protocol header here.
  union cros_ip_addr* dest_addr;    // write dest addr from ip header to here.
  union cros_ip_addr* source_addr;  // write source addr from ip header to here.
  // What is local vs remote changes on whether the packet is rx or tx.
  // If a packet is being received then local addr/port should be set to
  // the destination addr/port parsed from the packet and remote addr/port
  // should be set to the source addr/port parsed from the packet.
  if (isTx) {
    source_addr = &(five_tuple->local_addr);
    source_port = &(five_tuple->local_port);

    dest_addr = &(five_tuple->remote_addr);
    dest_port = &(five_tuple->remote_port);
  } else {  // Rx packet.
    source_addr = &(five_tuple->remote_addr);
    source_port = &(five_tuple->remote_port);

    dest_addr = &(five_tuple->local_addr);
    dest_port = &(five_tuple->local_port);
  }
  BPF_CORE_READ_INTO(&(source_addr->addr6), hdr, saddr.in6_u.u6_addr8);
  five_tuple->family = CROS_FAMILY_AF_INET6;
  BPF_CORE_READ_INTO(&(dest_addr->addr6), hdr, daddr.in6_u.u6_addr8);
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
      *source_port = bpf_ntohs(BPF_CORE_READ(tcp_header, source));
      *dest_port = bpf_ntohs(BPF_CORE_READ(tcp_header, dest));
      break;
    case CROS_NEXTHDR_UDP:
      five_tuple->protocol = CROS_PROTOCOL_UDP;
      const struct udphdr* udp_header =
          (const struct udphdr*)cros_skb_transport_header(skb);
      *source_port = bpf_ntohs(BPF_CORE_READ(udp_header, source));
      *dest_port = bpf_ntohs(BPF_CORE_READ(udp_header, dest));
      break;
    case -CROS_EBADMSG:
      five_tuple->protocol = CROS_PROTOCOL_UNKNOWN;
      break;
    default:
      // IPv6 header does not have a next header value reserved for raw sockets.
      // Use the socket type to determine if this is a raw socket.
      if (sk && BPF_CORE_READ(sk, sk_type) == SOCK_RAW) {
        five_tuple->protocol = CROS_PROTOCOL_RAW;
      } else {
        five_tuple->protocol = CROS_PROTOCOL_UNKNOWN;
      }
      break;
  }
  return packet_size;
}

static inline __attribute__((always_inline)) int cros_fill_ipv4_5_tuple(
    struct cros_network_5_tuple* five_tuple,
    const struct sk_buff* skb,
    bool isTx) {
  const struct iphdr* hdr = (struct iphdr*)(cros_skb_network_header(skb));
  uint16_t* dest_port;    // write dest port from protocol header to here.
  uint16_t* source_port;  // write source port from protocol header here.
  union cros_ip_addr* dest_addr;    // write dest addr from ip header to here.
  union cros_ip_addr* source_addr;  // write source addr from ip header to here.

  int protocol = BPF_CORE_READ(hdr, protocol);
  // What is local vs remote changes on whether the packet is rx or tx.
  // If a packet is being received then local addr/port should be set to
  // the destination addr/port parsed from the packet and remote addr/port
  // should be set to the source addr/port parsed from the packet.
  if (isTx) {
    source_addr = &(five_tuple->local_addr);
    source_port = &(five_tuple->local_port);

    dest_addr = &(five_tuple->remote_addr);
    dest_port = &(five_tuple->remote_port);
  } else {  // Rx packet
    source_addr = &(five_tuple->remote_addr);
    source_port = &(five_tuple->remote_port);

    dest_addr = &(five_tuple->local_addr);
    dest_port = &(five_tuple->local_port);
  }

  five_tuple->family = CROS_FAMILY_AF_INET;

  source_addr->addr4 = BPF_CORE_READ(hdr, saddr);
  dest_addr->addr4 = BPF_CORE_READ(hdr, daddr);
  // tot_len may not yet be filled out completely.
  int packet_size = bpf_ntohs(BPF_CORE_READ(hdr, tot_len));
  if (packet_size == 0) {
    packet_size = BPF_CORE_READ(skb, len);
  }

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
      *source_port = bpf_ntohs(BPF_CORE_READ(udp_header, source));
      *dest_port = bpf_ntohs(BPF_CORE_READ(udp_header, dest));
      break;
    case IPPROTO_TCP:
      five_tuple->protocol = CROS_PROTOCOL_TCP;
      const struct tcphdr* tcp_header =
          (const struct tcphdr*)cros_skb_transport_header(skb);
      *source_port = bpf_ntohs(BPF_CORE_READ(tcp_header, source));
      *dest_port = bpf_ntohs(BPF_CORE_READ(tcp_header, dest));
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
    five_tuple->remote_addr.addr4 = sk_common.skc_daddr;
    five_tuple->local_addr.addr4 = sk_common.skc_rcv_saddr;
  } else if (five_tuple->family == CROS_FAMILY_AF_INET6) {
    __builtin_memcpy(five_tuple->local_addr.addr6,
                     sk_common.skc_v6_rcv_saddr.in6_u.u6_addr8,
                     sizeof(five_tuple->local_addr.addr6));
    __builtin_memcpy(five_tuple->remote_addr.addr6,
                     sk_common.skc_v6_daddr.in6_u.u6_addr8,
                     sizeof(five_tuple->remote_addr.addr6));
  }
  five_tuple->remote_port = sk_common.skc_dport;
  five_tuple->local_port = sk_common.skc_num;
}

static inline __attribute__((always_inline)) bool fill_process_start(
    struct cros_process_start* process_start) {
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  int pid = BPF_CORE_READ(t, tgid);
  struct cros_process_start* process_start_from_exec =
      bpf_map_lookup_elem(&shared_process_info, &pid);
  if (process_start_from_exec != NULL) {
    __builtin_memcpy(&process_start->task_info,
                     &process_start_from_exec->task_info,
                     sizeof(process_start->task_info));
    __builtin_memcpy(&process_start->image_info,
                     &process_start_from_exec->image_info,
                     sizeof(process_start->image_info));
    __builtin_memcpy(&process_start->spawn_namespace,
                     &process_start_from_exec->spawn_namespace,
                     sizeof(process_start->spawn_namespace));
    return true;
  }
  cros_fill_task_info(&process_start->task_info, t);
  return false;
}

// Create a sk_storage and populate it. If storage already exists then do
// nothing.
static inline __attribute__((always_inline)) const struct cros_sk_info*
create_process_map_entry(struct socket* sock, const char* ctx) {
  struct sock* sk = sock->sk;
  if (!sk) {
    bpf_printk("create_process_map_entry failed: sock->sk is NULL");
    return NULL;
  }
  struct cros_sk_info* sk_info = cros_sk_storage_get_mutable(sk);
  if (sk_info != NULL) {
    return sk_info;
  }
  sk_info = cros_sk_storage_get_or_create(sk);
  if (!sk_info) {
    return sk_info;
  }
  sk_info->family = sk->__sk_common.skc_family;
  sk_info->protocol =
      determine_protocol(sk_info->family, sock->type, sk->sk_protocol);

  if (sk_info->protocol != CROS_PROTOCOL_TCP &&
      sk_info->protocol != CROS_PROTOCOL_UDP) {
    // In order to uniquely identify a socket (processes) that a network flow
    // belongs to a 5-tuple is required.
    // 5-tuples cannot be generated from ICMP/RAW flows due to the lack of port
    // numbers so add a sock_id to differentiate these flows between different
    // sockets(processes).
    if (cros_get_socket_id(sk, &(sk_info->sock_id)) != 0) {
      bpf_printk("create_process_map_entry failed, cros_get_socket_id failed");
      return NULL;
    }
  }
  sk_info->has_full_process_info = fill_process_start(&sk_info->process_start);
  cros_sk_storage_set(sk_info, sk);
  return sk_info;
}

static inline __attribute__((always_inline)) int cros_maybe_new_socket(
    struct socket* sock) {
  uint64_t sock_key = 0;
  if (cros_get_socket_id(sock->sk, &sock_key) != 0) {
    bpf_printk("cros_maybe_new_socket failed, cros_get_socket_id failed");
    return -1;
  }
  if (bpf_map_update_elem(&active_socket_map, &sock_key, &sock_key,
                          BPF_NOEXIST) != 0) {
    return -1;
  }
  return 0;
}

static inline __attribute__((always_inline)) int cros_new_flow_entry(
    const struct cros_sk_info* sk_info,
    struct cros_flow_map_key* map_key,
    enum cros_network_socket_direction direction,
    uint32_t tx_bytes,
    uint32_t rx_bytes) {
  struct cros_flow_map_value* value = &sk_info->flow_map_value_scratchpad;
  value->garbage_collect_me = false;
  value->sock_id = sk_info->sock_id;
  value->direction = direction;
  value->rx_bytes = rx_bytes;
  value->tx_bytes = tx_bytes;
  value->has_full_process_info = false;

  __builtin_memcpy(&value->process_info.task_info,
                   &sk_info->process_start.task_info,
                   sizeof(value->process_info.task_info));

  if (sk_info->has_full_process_info == true) {
    __builtin_memcpy(&value->process_info.image_info,
                     &sk_info->process_start.image_info,
                     sizeof(value->process_info.image_info));
    __builtin_memcpy(&value->process_info.spawn_namespace,
                     &sk_info->process_start.spawn_namespace,
                     sizeof(value->process_info.spawn_namespace));
    value->has_full_process_info = true;
  }

  if (bpf_map_update_elem(&cros_network_flow_map, map_key, value, BPF_NOEXIST) <
      0) {
    bpf_printk("WARNING: Could not create flow entry: Entry already exists.");
  }
  return 0;
}
static inline __attribute__((always_inline)) int cros_handle_tx_rx(
    const struct sk_buff* skb, bool is_tx, const char* calling_func_name) {
  if (!cros_is_ifindex_external(skb)) {
    return -CROS_IF_NOT_EXTERNAL;
  }

  struct cros_flow_map_value* flow_map_value;
  struct cros_flow_map_key flow_map_key;
  // Fun fact: BPF verifier will complain if the key contains
  // any uninitialized values.
  __builtin_memset(&flow_map_key, 0, sizeof(flow_map_key));
  int bytes = 0;

  bool is_ipv6 = false;
  const struct iphdr* hdr = (const struct iphdr*)(cros_skb_network_header(skb));
  uint8_t version = BPF_CORE_READ_BITFIELD_PROBED(hdr, version);
  if (version == 6) {
    is_ipv6 = true;
  } else if (version != 4) {
    return -CROS_UNSUPPORTED_IP_VERSION;
  }
  if (is_ipv6) {
    bytes = cros_fill_ipv6_5_tuple(&flow_map_key.five_tuple, skb, is_tx);
  } else {
    bytes = cros_fill_ipv4_5_tuple(&flow_map_key.five_tuple, skb, is_tx);
  }
  struct cros_sk_info* sk_info = NULL;
  if (is_tx) {
    /* skb->sk is only non-null on the transmit path.*/
    struct sock* sk = skb->sk;
    sk_info = NULL;
    if (sk) {
      sk_info = cros_sk_storage_get_mutable(sk);
    }
    if (!sk_info) {
      return -CROS_NO_SK_STORAGE_ALLOCATED;
    }
    flow_map_key.sock_id = sk_info->sock_id;
  } else {
    switch (flow_map_key.five_tuple.protocol) {
      case CROS_PROTOCOL_ICMP:
      case CROS_PROTOCOL_ICMP6:
      case CROS_PROTOCOL_RAW:
        /* TODO(b331290994):Fix rx byte counting for ICMP, ICMPV6 and raw
         * sockets.*/
        return -CROS_OTHER_ERROR;
        break;
    }
  }
  flow_map_value = bpf_map_lookup_elem(&cros_network_flow_map, &flow_map_key);
  if (flow_map_value) {  // entry already exists.
    if (is_tx) {
      flow_map_value->tx_bytes = flow_map_value->tx_bytes + bytes;
    } else {
      flow_map_value->rx_bytes = flow_map_value->rx_bytes + bytes;
    }
  } else {
    // The socket was likely in operation before this BPF program was loaded
    // so we can't be sure of the direction.
    enum cros_network_socket_direction dir = CROS_SOCKET_DIRECTION_UNKNOWN;
    if (is_tx) {
      if (!sk_info) {
        return -CROS_NO_SK_STORAGE_ALLOCATED;
      }
      return cros_new_flow_entry(/*sk_info=*/sk_info,
                                 /*map_key=*/&flow_map_key,
                                 /*direction=*/dir,
                                 /*tx_bytes*/ bytes, /*rx_bytes*/ 0);
    } else {
      /* socket storage is unavailable on the receive path.*/
      return -CROS_NO_NEW_FLOW_ON_RX;
    }
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/inet_listen", "tp_btf/cros_inet_listen_exit")
int BPF_PROG(cros_handle_inet_listen,
             struct socket* sock,
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
    bpf_printk("inet_listen unable to reserve buffer");
    return 0;
  }
  struct sock* sk = sock->sk;
  event->type = kNetworkEvent;
  event->data.network_event.type = kNetworkSocketListen;
  struct cros_network_socket_listen* sl =
      &(event->data.network_event.data.socket_listen);
  int socket_type = sock->type;
  sl->family = sk->__sk_common.skc_family;
  sl->protocol = determine_protocol(sl->family, socket_type, sk->sk_protocol);
  sl->has_full_process_info = fill_process_start(&sl->process_info);
  sl->socket_type = sock->type;
  // Extract out the source port.
  sl->port = sk->__sk_common.skc_num;
  // Fill out the IPv4 address.
  sl->ipv4_addr = sk->__sk_common.skc_rcv_saddr;
  __builtin_memcpy(sl->ipv6_addr,
                   sk->__sk_common.skc_v6_rcv_saddr.in6_u.u6_addr8,
                   sizeof(sl->ipv6_addr) / sizeof(uint8_t));
  bpf_ringbuf_submit(event, 0);
  return 0;
}

SEC("tp_btf/netif_receive_skb")
int BPF_PROG(cros_handle_netif_receive_skb, struct sk_buff* skb) {
  cros_handle_tx_rx(/* skb */ skb,
                    /* is_tx */ false, "netif_receive_skb");
  return 0;
}

SEC("tp_btf/net_dev_queue")
int BPF_PROG(cros_handle_trace_net_dev_queue, struct sk_buff* skb) {
  cros_handle_tx_rx(/* skb */ skb,
                    /* is_tx */ true, "cros_handle_trace_net_dev_queue");
  return 0;
}
CROS_IF_FUNCTION_HOOK("fexit/inet_accept", "tp_btf/cros_inet_accept_exit")
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
  const struct cros_sk_info* sk_info =
      create_process_map_entry(newsock, "inet_accept");
  if (!sk_info) {
    bpf_printk(
        "cros_network_accept_exit was unable to allocate and populate "
        "sk_info");
    return 0;
  }
  key.sock_id = sk_info->sock_id;
  cros_maybe_new_socket(newsock);
  cros_fill_5_tuple_from_sock(&key.five_tuple, newsock);
  // new socket, new process entry.
  value_ref = bpf_map_lookup_elem(&cros_network_flow_map, &key);
  if (value_ref) {  // entry already exist.. this shouldn't be the case.
    bpf_printk(
        "cros_network_accept_exit encountered a socket with existing flow "
        "entry");
  } else {  // entry does not exist so must be an outbound connection.
    cros_new_flow_entry(/*sk_info=*/sk_info, /*map_key=*/&key,
                        /*direction=*/CROS_SOCKET_DIRECTION_IN,
                        /*tx_bytes=*/0, /*rx_bytes=*/0);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/__inet_stream_connect",
                      "tp_btf/cros_inet_stream_connect_exit")
int BPF_PROG(cros_handle___inet_stream_connect_exit,
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
  const struct cros_sk_info* sk_info =
      create_process_map_entry(sock, "inet_connect");
  if (!sk_info) {
    bpf_printk("inet_connect was unable to allocate and populate sk_info");
    return 0;
  }
  key.sock_id = sk_info->sock_id;
  cros_maybe_new_socket(sock);
  cros_fill_5_tuple_from_sock(&key.five_tuple, sock);
  value_ref = bpf_map_lookup_elem(&cros_network_flow_map, &key);
  if (value_ref) {
    value_ref->direction = CROS_SOCKET_DIRECTION_OUT;
  } else {  // entry does not exist so must be an outbound connection.
    cros_new_flow_entry(/*sk_info=*/sk_info, /*map_key=*/&key,
                        /*direction=*/CROS_SOCKET_DIRECTION_OUT,
                        /*tx_bytes=*/0, /*rx_bytes=*/0);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fentry/inet_release", "tp_btf/cros_inet_release_enter")
int BPF_PROG(cros_handle_inet_release_enter, struct socket* sock) {
  uint64_t key = 0;
  if (cros_get_socket_id(sock->sk, &key) != 0) {
    bpf_printk(
        "inet_release failed to release a socket because cros_get_socket_id "
        "failed");
    return 0;
  }
  if (bpf_map_delete_elem(&active_socket_map, &key) == -1) {
    bpf_printk("inet_release: active socket deletion failed for %d.", key);
  }
  return 0;
}

CROS_IF_FUNCTION_HOOK("fentry/inet_sendmsg", "tp_btf/cros_inet_sendmsg_enter")
int BPF_PROG(cros_handle_inet_sendmsg_enter,
             struct socket* sock,
             struct msghdr* msg,
             size_t size) {
  // This is a safe area to grab process context.
  const struct cros_sk_info* sk_info =
      create_process_map_entry(sock, "inet_sendmsg");
  if (!sk_info) {
    bpf_printk("inet_sendmsg was unable to allocate and populate sk_info");
    return 0;
  }
  cros_maybe_new_socket(sock);
  return 0;
}

CROS_IF_FUNCTION_HOOK("fexit/inet_recvmsg", "tp_btf/cros_inet_recvmsg_exit")
int BPF_PROG(cros_handle_inet_recvmsg_exit,
             struct socket* sock,
             struct msghdr* msg,
             size_t size,
             int flags,
             int rv) {
  // This is a safe area to grab process context.
  const struct cros_sk_info* sk_info =
      create_process_map_entry(sock, "inet_recvmsg");
  if (!sk_info) {
    bpf_printk("inet_recvmsg was unable to allocate and populate sk_info");
    return 0;
  }
  cros_maybe_new_socket(sock);
  return 0;
}

// TODO(b/339679923): soft rollout on all platforms that support LSM hooks to
// gather support for adding downstream tracepoint patches to support ARM64.
#ifdef CROS_LSM_BPF_SUPPORTED
SEC("lsm/socket_post_create")
int BPF_PROG(cros_handle_socket_post_create,
             struct socket* sock,
             int family,
             int type,
             int protocol,
             int kern) {
  if (kern) {
    return 0;
  }
  const struct cros_sk_info* sk_info =
      create_process_map_entry(sock, "socket_post_create");
  if (!sk_info) {
    bpf_printk(
        "socket_post_create was unable to allocate and populate sk_info");
    return 0;
  }
  cros_maybe_new_socket(sock);
  return 0;
}
#endif
