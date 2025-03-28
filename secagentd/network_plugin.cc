// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "absl/types/span.h"
#include "base/base64.h"
#include "base/containers/lru_cache.h"
#include "base/hash/sha1.h"
#include "base/sys_byteorder.h"
#include "secagentd/batch_sender.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {
namespace pb = cros_xdr::reporting;
namespace bpf {
bool operator<(const secagentd::bpf::cros_flow_map_key& lhs,
               const secagentd::bpf::cros_flow_map_key& rhs) {
  auto& lhs_tuple = lhs.five_tuple;
  absl::Span lhs_daddr6 = lhs_tuple.remote_addr.addr6;
  absl::Span lhs_saddr6 = lhs_tuple.local_addr.addr6;

  auto& rhs_tuple = rhs.five_tuple;
  absl::Span rhs_raddr6 = rhs_tuple.remote_addr.addr6;
  absl::Span rhs_laddr6 = rhs_tuple.local_addr.addr6;

  return std::tie(lhs_tuple.family, lhs_tuple.protocol,
                  lhs_tuple.remote_addr.addr4, lhs_tuple.local_addr.addr4,
                  lhs_daddr6, lhs_saddr6, lhs_tuple.local_port,
                  lhs_tuple.remote_port, lhs.sock_id) <
         std::tie(rhs_tuple.family, rhs_tuple.protocol,
                  rhs_tuple.remote_addr.addr4, rhs_tuple.local_addr.addr4,
                  rhs_raddr6, rhs_laddr6, rhs_tuple.local_port,
                  rhs_tuple.remote_port, rhs.sock_id);
}
}  // namespace bpf

namespace {
pb::NetworkProtocol BpfProtocolToPbProtocol(
    bpf::cros_network_protocol protocol) {
  pb::NetworkProtocol rv;
  switch (protocol) {
    case bpf::CROS_PROTOCOL_ICMP:
    case bpf::CROS_PROTOCOL_ICMP6:
      rv = pb::NetworkProtocol::ICMP;
      break;
    case bpf::CROS_PROTOCOL_RAW:
      rv = pb::NetworkProtocol::RAW;
      break;
    case bpf::CROS_PROTOCOL_TCP:
      rv = pb::NetworkProtocol::TCP;
      break;
    case bpf::CROS_PROTOCOL_UDP:
      rv = pb::NetworkProtocol::UDP;
      break;
    case bpf::CROS_PROTOCOL_UNKNOWN:
      rv = pb::NetworkProtocol::NETWORK_PROTOCOL_UNKNOWN;
      break;
  }
  return rv;
}

bool IsFilteredOut(const pb::NetworkFlowEvent& flow_event) {
  if (!flow_event.has_process() || !flow_event.has_network_flow()) {
    return true;
  }
  const auto process = flow_event.process();
  const auto flow = flow_event.network_flow();

  // patchpanel SSDP creates an incredible amount of network spam
  // in certain environments.
  if (flow.has_protocol() && flow.protocol() == pb::UDP &&
      flow.has_local_port() && flow.local_port() == 1900) {
    if (flow.has_local_ip() && (flow.local_ip() == "239.255.255.250" ||
                                flow.local_ip().starts_with("ff05::c"))) {
      if (process.has_image() && process.image().has_pathname() &&
          process.image().pathname().starts_with("/usr/bin/patchpaneld")) {
        return true;
      }
    }
  }

  // Filter out avahi-daemon spam.
  if (process.commandline().starts_with("\'avahi-daemon: running")) {
    return true;
  }

  // Filter out obvious local traffic.
  if (flow.has_remote_ip() && flow.remote_ip() == "127.0.0.1") {
    return true;
  }

  return false;
}

}  // namespace

NetworkPlugin::NetworkPlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      process_cache_(process_cache),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user),
      batch_sender_(
          std::make_unique<BatchSender<std::string,
                                       pb::XdrNetworkEvent,
                                       pb::NetworkEventAtomicVariant>>(
              base::BindRepeating(
                  [](const cros_xdr::reporting::NetworkEventAtomicVariant&)
                      -> std::string {
                    // TODO(b:282814056): Make hashing function optional
                    //  for batch_sender then drop this. Not all users
                    //  of batch_sender need the visit functionality.
                    return "";
                  }),
              message_sender,
              reporting::Destination::CROS_SECURITY_NETWORK,
              batch_interval_s)),
      bpf_skeleton_helper_(
          std::make_unique<BpfSkeletonHelper<Types::BpfSkeleton::kNetwork>>(
              bpf_skeleton_factory, batch_interval_s)) {
  CHECK(message_sender != nullptr);
  CHECK(process_cache != nullptr);
  CHECK(bpf_skeleton_factory);
  prev_tx_rx_totals_ = std::make_unique<
      base::LRUCache<bpf::cros_flow_map_key, bpf::cros_flow_map_value>>(
      bpf::kMaxFlowMapEntries);
}

absl::Status NetworkPlugin::Activate() {
  struct BpfCallbacks callbacks;
  callbacks.ring_buffer_event_callback = base::BindRepeating(
      &NetworkPlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());

  absl::Status status = bpf_skeleton_helper_->LoadAndAttach(callbacks);
  if (status == absl::OkStatus()) {
    batch_sender_->Start();
  }
  return status;
}

absl::Status NetworkPlugin::Deactivate() {
  return bpf_skeleton_helper_->DetachAndUnload();
}

bool NetworkPlugin::IsActive() const {
  return bpf_skeleton_helper_->IsAttached();
}

std::string NetworkPlugin::GetName() const {
  return "Network";
}

void NetworkPlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  auto atomic_event = std::make_unique<pb::NetworkEventAtomicVariant>();
  if (bpf_event.type != bpf::kNetworkEvent) {
    LOG(ERROR) << "Unexpected BPF event type.";
    return;
  }
  const bpf::cros_network_event& ne = bpf_event.data.network_event;
  if (ne.type == bpf::kSyntheticNetworkFlow) {
    // Synthetic Network Flow events are synthesized by the NetworkBpfSkeleton
    // These events are synthesized by scanning a BPF map and converting each
    // map entry into a cros_event and then calling the HandleRingBufferEvent
    // callback.
    auto flow_proto = MakeFlowEvent(ne.data.flow);
    if (flow_proto == nullptr) {
      // The flow event was synthesized from a map entry that wasn't updated
      // since the last map scan, so discard the event.
      return;
    }
    atomic_event->set_allocated_network_flow(flow_proto.release());
  } else if (ne.type == bpf::kNetworkSocketListen) {
    atomic_event->set_allocated_network_socket_listen(
        MakeListenEvent(ne.data.socket_listen).release());
  }

  device_user_->GetDeviceUserAsync(
      base::BindOnce(&NetworkPlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(atomic_event)));
}

std::string NetworkPlugin::ComputeCommunityHashv1(
    const absl::Span<const uint8_t>& source_address_in,
    const absl::Span<const uint8_t>& destination_address_in,
    uint16_t source_port,
    uint16_t destination_port,
    uint8_t proto,
    uint16_t seed) {
  std::vector<uint8_t> source_address(source_address_in.begin(),
                                      source_address_in.end());
  std::vector<uint8_t> destination_address(destination_address_in.begin(),
                                           destination_address_in.end());
  // Check to make sure the IP addresses are the correct length for
  // ipv4 or ipv6 and that dest and source are the same size.
  if ((destination_address.size() != source_address.size()) ||
      (destination_address.size() != 16 && destination_address.size() != 4)) {
    return "";
  }
  CHECK(destination_address.size() == source_address.size());
  CHECK(source_address.size() == 16 || source_address.size() == 4);
  std::vector<uint8_t> buff_to_hash;
  auto push_short = [&buff_to_hash](uint16_t s) {
    uint16_t nbo = base::HostToNet16(s);
    buff_to_hash.push_back(0xFF & (nbo >> 8));
    buff_to_hash.push_back(0xFF & nbo);
  };
  buff_to_hash.push_back(0);
  buff_to_hash.push_back(0);
  source_port = base::HostToNet16(source_port);
  destination_port = base::HostToNet16(destination_port);
  auto append_addr_port =
      [&buff_to_hash, &proto, &push_short](
          const std::vector<uint8_t>& first_addr, uint16_t first_port,
          const std::vector<uint8_t>& second_addr, uint16_t second_port) {
        buff_to_hash.insert(buff_to_hash.end(), first_addr.begin(),
                            first_addr.end());
        buff_to_hash.insert(buff_to_hash.end(), second_addr.begin(),
                            second_addr.end());
        buff_to_hash.push_back(proto);
        buff_to_hash.push_back(0);
        push_short(first_port);
        push_short(second_port);
      };

  // Order it so that the smaller IP:port tuple comes first in the
  // buffer to hash.
  // The addresses are in network byte order so most significant
  // byte is index 0.
  for (int idx = 0; idx < source_address.size(); idx++) {
    if (source_address[idx] < destination_address[idx]) {
      append_addr_port(source_address, source_port, destination_address,
                       destination_port);
      break;
    } else if (source_address[idx] > destination_address[idx]) {
      append_addr_port(destination_address, destination_port, source_address,
                       source_port);
      break;
    } else if (idx == source_address.size() - 1) {
      // IP addresses are identical.
      if (source_port < destination_port) {
        append_addr_port(source_address, source_port, destination_address,
                         destination_port);
      } else {
        append_addr_port(destination_address, destination_port, source_address,
                         source_port);
      }
    }
  }
  auto digest = base::SHA1Hash(buff_to_hash);
  std::string community_hash{"1:"};
  base::Base64EncodeAppend(digest, &community_hash);
  return community_hash;
}

void NetworkPlugin::EnqueueBatchedEvent(
    std::unique_ptr<pb::NetworkEventAtomicVariant> atomic_event) {
  batch_sender_->Enqueue(std::move(atomic_event));
}

void NetworkPlugin::OnDeviceUserRetrieved(
    std::unique_ptr<pb::NetworkEventAtomicVariant> atomic_event,
    const std::string& device_user,
    const std::string& device_userhash) {
  atomic_event->mutable_common()->set_device_user(device_user);
  EnqueueBatchedEvent(std::move(atomic_event));
}

std::unique_ptr<pb::NetworkSocketListenEvent> NetworkPlugin::MakeListenEvent(
    const bpf::cros_network_socket_listen& l) const {
  auto listen_proto = std::make_unique<pb::NetworkSocketListenEvent>();
  auto* socket = listen_proto->mutable_socket();
  if (l.family == bpf::CROS_FAMILY_AF_INET) {
    std::array<char, INET_ADDRSTRLEN> buff4;
    if (inet_ntop(AF_INET, &l.ipv4_addr, buff4.data(), buff4.size()) !=
        nullptr) {
      socket->set_bind_addr(buff4.data());
    }
  } else if (l.family == bpf::CROS_FAMILY_AF_INET6) {
    std::array<char, INET6_ADDRSTRLEN> buff6;
    if (inet_ntop(AF_INET6, l.ipv6_addr, buff6.data(), buff6.size()) !=
        nullptr) {
      socket->set_bind_addr(buff6.data());
    }
  }
  socket->set_bind_port(l.port);
  socket->set_protocol(BpfProtocolToPbProtocol(l.protocol));
  switch (l.socket_type) {
    case __socket_type::SOCK_STREAM:
      socket->set_socket_type(pb::SocketType::SOCK_STREAM);
      break;
    case __socket_type::SOCK_DGRAM:
      socket->set_socket_type(pb::SocketType::SOCK_DGRAM);
      break;
    case __socket_type::SOCK_SEQPACKET:
      socket->set_socket_type(pb::SocketType::SOCK_SEQPACKET);
      break;
    case __socket_type::SOCK_RAW:
      socket->set_socket_type(pb::SocketType::SOCK_RAW);
      break;
    case __socket_type::SOCK_RDM:
      socket->set_socket_type(pb::SocketType::SOCK_RDM);
      break;
    case __socket_type::SOCK_PACKET:
      socket->set_socket_type(pb::SocketType::SOCK_PACKET);
      break;
  }

  ProcessCache::FillProcessTree(listen_proto.get(), l.process_info,
                                l.has_full_process_info, process_cache_,
                                device_user_);
  return listen_proto;
}

std::unique_ptr<cros_xdr::reporting::NetworkFlowEvent>
NetworkPlugin::MakeFlowEvent(
    const secagentd::bpf::cros_synthetic_network_flow& flow_event) const {
  auto flow_proto = std::make_unique<pb::NetworkFlowEvent>();
  auto* flow = flow_proto->mutable_network_flow();
  auto& five_tuple = flow_event.flow_map_key.five_tuple;
  bpf::cros_flow_map_key k = flow_event.flow_map_key;

  auto it = prev_tx_rx_totals_->Get(k);

  if (it == prev_tx_rx_totals_->end()) {
    flow->set_rx_bytes(flow_event.flow_map_value.rx_bytes);
    flow->set_tx_bytes(flow_event.flow_map_value.tx_bytes);
    if (!flow_event.flow_map_value.garbage_collect_me) {
      prev_tx_rx_totals_->Put(flow_event.flow_map_key,
                              flow_event.flow_map_value);
    }
  } else {
    auto rx_bytes = flow_event.flow_map_value.rx_bytes - it->second.rx_bytes;
    auto tx_bytes = flow_event.flow_map_value.tx_bytes - it->second.tx_bytes;
    if (rx_bytes == 0 && tx_bytes == 0) {
      // No change to tx/rx bytes , consider it an uninteresting event.
      return nullptr;
    }
    it->second.rx_bytes = flow_event.flow_map_value.rx_bytes;
    it->second.tx_bytes = flow_event.flow_map_value.tx_bytes;
    flow->set_rx_bytes(rx_bytes);
    flow->set_tx_bytes(tx_bytes);
    if (flow_event.flow_map_value.garbage_collect_me) {
      prev_tx_rx_totals_->Erase(it);
    }
  }

  /* default to ipv4 */
  const void* local_addr_ptr = &five_tuple.local_addr.addr4;
  const void* remote_addr_ptr = &five_tuple.remote_addr.addr4;
  int af = AF_INET;
  std::array<char, INET6_ADDRSTRLEN> buff;
  if (five_tuple.family == bpf::CROS_FAMILY_AF_INET6) {
    // ipv6
    af = AF_INET6;
    local_addr_ptr = &five_tuple.local_addr.addr6;
    remote_addr_ptr = &five_tuple.remote_addr.addr6;
    auto src = absl::MakeSpan(five_tuple.local_addr.addr6,
                              sizeof(five_tuple.local_addr.addr6));
    auto dest = absl::MakeSpan(five_tuple.remote_addr.addr6,
                               sizeof(five_tuple.remote_addr.addr6));
    flow->set_community_id_v1(
        ComputeCommunityHashv1(src, dest, five_tuple.local_port,
                               five_tuple.remote_port, five_tuple.protocol));
  } else {
    // ipv4
    auto src = absl::MakeSpan(
        reinterpret_cast<const uint8_t*>(&five_tuple.local_addr.addr4),
        sizeof(five_tuple.local_addr.addr4));
    auto dest = absl::MakeSpan(
        reinterpret_cast<const uint8_t*>(&five_tuple.remote_addr.addr4),
        sizeof(five_tuple.remote_addr.addr4));
    flow->set_community_id_v1(
        ComputeCommunityHashv1(src, dest, five_tuple.local_port,
                               five_tuple.remote_port, five_tuple.protocol));
  }
  if (inet_ntop(af, local_addr_ptr, buff.data(), buff.size()) != nullptr) {
    flow->set_local_ip(buff.data());
  }
  if (inet_ntop(af, remote_addr_ptr, buff.data(), buff.size()) != nullptr) {
    flow->set_remote_ip(buff.data());
  }
  flow->set_local_port(five_tuple.local_port);
  flow->set_remote_port(five_tuple.remote_port);
  flow->set_protocol(BpfProtocolToPbProtocol(five_tuple.protocol));
  switch (flow_event.flow_map_value.direction) {
    case bpf::cros_network_socket_direction::CROS_SOCKET_DIRECTION_IN:
      flow->set_direction(pb::NetworkFlow::INCOMING);
      break;
    case bpf::cros_network_socket_direction::CROS_SOCKET_DIRECTION_OUT:
      flow->set_direction(pb::NetworkFlow::OUTGOING);
      break;
    case bpf::cros_network_socket_direction::CROS_SOCKET_DIRECTION_UNKNOWN:
      flow->set_direction(pb::NetworkFlow::DIRECTION_UNKNOWN);
      break;
  }

  ProcessCache::FillProcessTree(flow_proto.get(),
                                flow_event.flow_map_value.process_info,
                                flow_event.flow_map_value.has_full_process_info,
                                process_cache_, device_user_);
  // TODO(b:294579287): Make event filtering more generic, before doing that
  // process cache hits need to be drastically improved.
  if (IsFilteredOut(*flow_proto)) {
    return nullptr;
  }
  return flow_proto;
}

}  // namespace secagentd
