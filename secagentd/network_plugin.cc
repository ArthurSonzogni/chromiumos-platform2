// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/plugins.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include "absl/strings/str_format.h"
#include "base/strings/string_util.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/message_sender.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

namespace pb = cros_xdr::reporting;

std::string NetworkPlugin::GetName() const {
  return "Network";
}

void NetworkPlugin::EnqueueBatchedEvent(
    std::unique_ptr<pb::NetworkEventAtomicVariant> atomic_event) {
  batch_sender_->Enqueue(std::move(atomic_event));
}

void NetworkPlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  auto atomic_event = std::make_unique<pb::NetworkEventAtomicVariant>();
  if (bpf_event.type != bpf::kNetworkEvent) {
    LOG(ERROR) << "Unexpected BPF event type.";
    return;
  }
  const bpf::cros_network_event& ne = bpf_event.data.network_event;
  if (ne.type == bpf::kSyntheticNetworkFlow) {
    // TODO(b:264550046): Fill out the network flow event using the process
    // cache etc..
  } else if (ne.type == bpf::kNetworkSocketListen) {
    atomic_event->set_allocated_network_socket_listen(
        MakeListenEvent(ne.data.socket_listen).release());
  }
  atomic_event->mutable_common()->set_device_user(
      device_user_->GetDeviceUser());
  EnqueueBatchedEvent(std::move(atomic_event));
}

std::unique_ptr<pb::NetworkSocketListenEvent> NetworkPlugin::MakeListenEvent(
    const bpf::cros_network_socket_listen& l) const {
  auto listen_proto = std::make_unique<pb::NetworkSocketListenEvent>();
  auto* socket = listen_proto->mutable_socket();
  if (l.common.family == bpf::CROS_FAMILY_AF_INET) {
    std::array<char, INET_ADDRSTRLEN> buff4;
    inet_ntop(AF_INET, &l.ipv4_addr, buff4.data(), buff4.size());
    socket->set_bind_addr(buff4.data());
  } else if (l.common.family == bpf::CROS_FAMILY_AF_INET6) {
    std::array<char, INET6_ADDRSTRLEN> buff6;
    inet_ntop(AF_INET6, l.ipv6_addr, buff6.data(), buff6.size());
    socket->set_bind_addr(buff6.data());
  }
  socket->set_bind_port(l.port);
  switch (l.common.protocol) {
    case bpf::CROS_PROTOCOL_ICMP:
      socket->set_protocol(pb::NetworkProtocol::ICMP);
      break;
    case bpf::CROS_PROTOCOL_RAW:
      socket->set_protocol(pb::NetworkProtocol::RAW);
      break;
    case bpf::CROS_PROTOCOL_TCP:
      socket->set_protocol(pb::NetworkProtocol::TCP);
      break;
    case bpf::CROS_PROTOCOL_UDP:
      socket->set_protocol(pb::NetworkProtocol::UDP);
      break;
    case bpf::CROS_PROTOCOL_UNKNOWN:
      socket->set_protocol(pb::NetworkProtocol::NETWORK_PROTOCOL_UNKNOWN);
      break;
  }
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
  auto hierarchy = process_cache_->GetProcessHierarchy(
      l.common.process.pid, l.common.process.start_time, 2);
  if (hierarchy.empty()) {
    LOG(ERROR) << absl::StrFormat(
        "ProcessCache hierarchy fetch for pid %d failed. Creating a "
        "NetworkSocketListen with unpopulated process and parent_process "
        "fields.",
        l.common.process.pid);
  }
  if (hierarchy.size() >= 1) {
    listen_proto->set_allocated_process(hierarchy[0].release());
  }
  if (hierarchy.size() == 2) {
    listen_proto->set_allocated_parent_process(hierarchy[1].release());
  }
  return listen_proto;
}

}  // namespace secagentd
