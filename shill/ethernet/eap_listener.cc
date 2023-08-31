// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/eap_listener.h"

#include <string>
#include <utility>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>

#include <base/compiler_specific.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <net-base/byte_utils.h>

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
  auto socket = CreateSocket();
  if (!socket) {
    LOG(ERROR) << LoggingTag() << ": Could not open EAP listener socket";
    return false;
  }

  socket_ = std::move(socket);
  socket_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      socket_->Get(), base::BindRepeating(&EapListener::ReceiveRequest,
                                          base::Unretained(this)));
  if (!socket_watcher_) {
    LOG(ERROR) << LoggingTag() << ": Failed on watching EAP listener socket";
    return false;
  }
  return true;
}

void EapListener::Stop() {
  socket_watcher_.reset();
  socket_.reset();
}

std::unique_ptr<net_base::Socket> EapListener::CreateSocket() {
  auto socket = socket_factory_->Create(PF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC,
                                        htons(ETH_P_PAE));
  if (!socket) {
    PLOG(ERROR) << LoggingTag() << ": Could not create EAP listener socket";
    return nullptr;
  }

  if (!socket->SetNonBlocking()) {
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
