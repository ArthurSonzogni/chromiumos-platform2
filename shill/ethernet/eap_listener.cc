// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/eap_listener.h"

#include <algorithm>
#include <string>
#include <utility>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>

#include <base/compiler_specific.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/mac_address.h>

#include "shill/ethernet/eap_protocol.h"
#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kEthernet;
}  // namespace Logging

const size_t EapListener::kMaxEapPacketLength =
    sizeof(eap_protocol::Ieee8021xHdr) + sizeof(eap_protocol::EapHeader);

EapListener::EapListener(int interface_index, const std::string& link_name)
    : interface_index_(interface_index), link_name_(link_name) {}

EapListener::~EapListener() = default;

bool EapListener::Start() {
  std::unique_ptr<net_base::Socket> socket = CreateSocket();
  if (!socket) {
    LOG(ERROR) << LoggingTag() << ": Could not open EAP listener socket";
    return false;
  }

  socket_ = std::move(socket);
  socket_->SetReadableCallback(base::BindRepeating(&EapListener::ReceiveRequest,
                                                   base::Unretained(this)));
  return true;
}

bool EapListener::EapMulticastMembership(const net_base::Socket& socket,
                                         MultiCastMembershipAction action) {
  // The next non-TPMR switch multicast address for EAPOL is 01-80-C2-00-00-03
  static constexpr net_base::MacAddress multi_addr =
      net_base::MacAddress(0x01, 0x80, 0xc2, 0x00, 0x00, 0x03);

  packet_mreq mr;
  memset(&mr, 0, sizeof(mr));
  mr.mr_ifindex = interface_index_;
  mr.mr_type = PACKET_MR_MULTICAST;
  mr.mr_alen = multi_addr.kAddressLength;
  memcpy(&mr.mr_address, multi_addr.data(),
         std::min(multi_addr.kAddressLength, sizeof(mr.mr_address)));

  if (!socket.SetSockOpt(SOL_PACKET,
                         action == MultiCastMembershipAction::Add
                             ? PACKET_ADD_MEMBERSHIP
                             : PACKET_DROP_MEMBERSHIP,
                         net_base::byte_utils::AsBytes(mr))) {
    PLOG(ERROR) << LoggingTag() << ": Could not "
                << (action == MultiCastMembershipAction::Add ? "add" : "remove")
                << " the EAP multicast address membership";
    return false;
  }
  SLOG(2) << LoggingTag() << ": success "
          << (action == MultiCastMembershipAction::Add ? "adding" : "removing")
          << " the EAP multicast address membership";
  return true;
}

void EapListener::Stop() {
  if (socket_) {
    // It is OK to remove the membership as wpa_supplicant will add the
    // multicast membership itself before sending the EAP response.
    EapMulticastMembership(*socket_, MultiCastMembershipAction::Remove);
  }
  socket_.reset();
}

std::unique_ptr<net_base::Socket> EapListener::CreateSocket() {
  std::unique_ptr<net_base::Socket> socket = socket_factory_->Create(
      PF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_PAE));
  if (!socket) {
    PLOG(ERROR) << LoggingTag() << ": Could not create EAP listener socket";
    return nullptr;
  }

  if (!base::SetNonBlocking(socket->Get())) {
    PLOG(ERROR) << LoggingTag() << ": Could not set socket to be non-blocking";
    return nullptr;
  }

  sockaddr_ll socket_address;
  memset(&socket_address, 0, sizeof(socket_address));
  socket_address.sll_family = AF_PACKET;
  socket_address.sll_protocol = htons(ETH_P_PAE);
  socket_address.sll_ifindex = interface_index_;

  if (!socket->Bind(reinterpret_cast<struct sockaddr*>(&socket_address),
                    sizeof(socket_address))) {
    PLOG(ERROR) << LoggingTag() << ": Could not bind socket to interface";
    return nullptr;
  }
  // Add the multicast membership for this listener to ensure the
  // initial EAP Request Identity frame from the authenticator is
  // received by shill.
  // See b/331503151 for details.
  EapMulticastMembership(*socket, MultiCastMembershipAction::Add);

  return socket;
}

void EapListener::ReceiveRequest() {
  struct {
    eap_protocol::Ieee8021xHdr onex_header;
    eap_protocol::EapHeader eap_header;
  } __attribute__((packed)) payload;
  sockaddr_ll remote_address;
  memset(&remote_address, 0, sizeof(remote_address));
  socklen_t socklen = sizeof(remote_address);
  const auto result = socket_->RecvFrom(
      net_base::byte_utils::AsMutBytes(payload), 0,
      reinterpret_cast<struct sockaddr*>(&remote_address), &socklen);
  if (!result) {
    PLOG(ERROR) << LoggingTag() << ": Socket recvfrom failed";
    Stop();
    return;
  }
  if (result != sizeof(payload)) {
    LOG(INFO) << LoggingTag() << ": Short EAP packet received";
    return;
  }

  SLOG(2) << LoggingTag() << ": EAP Request packet received" << std::hex
          << " version=0x" << static_cast<int>(payload.onex_header.version)
          << " type=0x" << static_cast<int>(payload.onex_header.type)
          << " code=0x" << static_cast<int>(payload.eap_header.code);
  if (payload.onex_header.version < eap_protocol::kIeee8021xEapolVersion1 ||
      payload.onex_header.type != eap_protocol::kIIeee8021xTypeEapPacket ||
      payload.eap_header.code != eap_protocol::kEapCodeRequest) {
    LOG(INFO) << LoggingTag() << ": Packet is not a valid EAP request";
    return;
  }
  LOG(INFO) << LoggingTag()
            << ": EAP request received with version=" << std::hex
            << static_cast<int>(payload.onex_header.version);

  request_received_callback_.Run();
}

const std::string& EapListener::LoggingTag() {
  return link_name_;
}

}  // namespace shill
