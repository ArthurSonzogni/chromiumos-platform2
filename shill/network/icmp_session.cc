// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/icmp_session.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>

#include <base/check_op.h>
#include <base/containers/span.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/socket.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace {
const int kIPHeaderLengthUnitBytes = 4;
}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

uint16_t IcmpSession::kNextUniqueEchoId = 0;

std::unique_ptr<IcmpSession> IcmpSession::CreateForTesting(
    EventDispatcher* dispatcher,
    std::unique_ptr<net_base::SocketFactory> socket_factory,
    int echo_id) {
  auto icmp_session =
      std::make_unique<IcmpSession>(dispatcher, std::move(socket_factory));
  icmp_session->echo_id_ = echo_id;
  return icmp_session;
}

IcmpSession::IcmpSession(
    EventDispatcher* dispatcher,
    std::unique_ptr<net_base::SocketFactory> socket_factory)
    : dispatcher_(dispatcher),
      socket_factory_(std::move(socket_factory)),
      echo_id_(kNextUniqueEchoId) {
  // Each IcmpSession will have a unique echo ID to identify requests and reply
  // messages.
  ++kNextUniqueEchoId;
}

IcmpSession::~IcmpSession() {
  Stop();
}

bool IcmpSession::Start(const net_base::IPAddress& destination,
                        int interface_index,
                        std::string_view interface_name,
                        IcmpSessionResultCallback result_callback) {
  if (!dispatcher_) {
    LOG(ERROR) << "Invalid dispatcher";
    return false;
  }
  if (IsStarted()) {
    LOG(WARNING) << "ICMP session already started";
    return false;
  }

  std::unique_ptr<net_base::Socket> socket;
  switch (destination.GetFamily()) {
    case net_base::IPFamily::kIPv4:
      socket = socket_factory_->Create(AF_INET, SOCK_RAW | SOCK_CLOEXEC,
                                       IPPROTO_ICMP);
      break;
    case net_base::IPFamily::kIPv6:
      socket = socket_factory_->Create(AF_INET6, SOCK_RAW | SOCK_CLOEXEC,
                                       IPPROTO_ICMPV6);
      break;
  }
  if (socket == nullptr) {
    PLOG(ERROR) << "Could not create ICMP socket";
    return false;
  }
  if (!base::SetNonBlocking(socket->Get())) {
    PLOG(ERROR) << "Could not set socket to be non-blocking";
    return false;
  }

  if (interface_name.size() >= IFNAMSIZ) {
    LOG(ERROR) << "The interface name '" << interface_name << "' is too long";
    return false;
  }
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  memcpy(ifr.ifr_name, interface_name.data(), interface_name.size());
  if (!socket->SetSockOpt(SOL_SOCKET, SO_BINDTODEVICE,
                          net_base::byte_utils::AsBytes(ifr))) {
    PLOG(ERROR) << "Failed to bind socket on " << interface_name;
    return false;
  }

  socket_ = std::move(socket);
  destination_ = destination;
  interface_index_ = interface_index;
  result_callback_ = std::move(result_callback);

  socket_->SetReadableCallback(base::BindRepeating(&IcmpSession::OnIcmpReadable,
                                                   base::Unretained(this)));
  timeout_callback_.Reset(BindOnce(&IcmpSession::ReportResultAndStopSession,
                                   weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, timeout_callback_.callback(),
                               kTimeout);
  seq_num_to_sent_recv_time_.clear();
  received_echo_reply_seq_numbers_.clear();
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&IcmpSession::TransmitEchoRequestTask,
                                       weak_ptr_factory_.GetWeakPtr()));

  return true;
}

void IcmpSession::Stop() {
  if (!IsStarted()) {
    return;
  }
  timeout_callback_.Cancel();

  socket_ = nullptr;
}

bool IcmpSession::IsStarted() const {
  return socket_ != nullptr;
}

// static
bool IcmpSession::AnyRepliesReceived(const IcmpSessionResult& result) {
  for (const base::TimeDelta& latency : result) {
    if (!latency.is_zero()) {
      return true;
    }
  }
  return false;
}

// static
bool IcmpSession::IsPacketLossPercentageGreaterThan(
    const IcmpSessionResult& result, int percentage_threshold) {
  if (percentage_threshold < 0) {
    LOG(ERROR) << __func__ << ": negative percentage threshold ("
               << percentage_threshold << ")";
    return false;
  }

  if (result.empty()) {
    return false;
  }

  int lost_packet_count = 0;
  for (const base::TimeDelta& latency : result) {
    if (latency.is_zero()) {
      ++lost_packet_count;
    }
  }
  int packet_loss_percentage = (lost_packet_count * 100) / result.size();
  return packet_loss_percentage > percentage_threshold;
}

// static
uint16_t IcmpSession::ComputeIcmpChecksum(const struct icmphdr& hdr,
                                          size_t len) {
  // Compute Internet Checksum for "len" bytes beginning at location "hdr".
  // Adapted directly from the canonical implementation in RFC 1071 Section 4.1.
  uint32_t sum = 0;
  const uint16_t* addr = reinterpret_cast<const uint16_t*>(&hdr);

  while (len > 1) {
    sum += *addr;
    ++addr;
    len -= sizeof(*addr);
  }

  // Add left-over byte, if any.
  if (len > 0) {
    sum += *reinterpret_cast<const uint8_t*>(addr);
  }

  // Fold 32-bit sum to 16 bits.
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return static_cast<uint16_t>(~sum);
}

void IcmpSession::TransmitEchoRequestTask() {
  if (!IsStarted()) {
    // This might happen when ping times out or is stopped between two calls
    // to IcmpSession::TransmitEchoRequestTask.
    return;
  }

  DCHECK(destination_);
  const bool success =
      (destination_->GetFamily() == net_base::IPFamily::kIPv4)
          ? TransmitV4EchoRequest(*destination_->ToIPv4Address())
          : TransmitV6EchoRequest(*destination_->ToIPv6Address());
  if (success) {
    seq_num_to_sent_recv_time_[current_sequence_number_] =
        std::make_pair(base::TimeTicks::Now(), base::TimeTicks());
  }
  ++current_sequence_number_;
  // If we fail to transmit the echo request, fall through instead of returning,
  // so we continue sending echo requests until |kTotalNumEchoRequests| echo
  // requests are sent.

  if (seq_num_to_sent_recv_time_.size() != kTotalNumEchoRequests) {
    dispatcher_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&IcmpSession::TransmitEchoRequestTask,
                       weak_ptr_factory_.GetWeakPtr()),
        kEchoRequestInterval);
  }
}

bool IcmpSession::TransmitV4EchoRequest(const net_base::IPv4Address& address) {
  struct icmphdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  icmp_header.type = ICMP_ECHO;
  icmp_header.code = kIcmpEchoCode;
  icmp_header.un.echo.id = echo_id_;
  icmp_header.un.echo.sequence = current_sequence_number_;
  icmp_header.checksum = ComputeIcmpChecksum(icmp_header, sizeof(icmp_header));
  const base::span<const uint8_t> payload = {
      reinterpret_cast<const uint8_t*>(&icmp_header), sizeof(icmp_header)};

  struct sockaddr_in destination_address;
  destination_address.sin_family = AF_INET;
  destination_address.sin_addr = address.ToInAddr();

  const std::optional<size_t> result = socket_->SendTo(
      payload, 0, reinterpret_cast<struct sockaddr*>(&destination_address),
      sizeof(destination_address));
  if (!result) {
    PLOG(ERROR) << "Socket sendto failed";
  } else if (result < payload.size()) {
    LOG(ERROR) << "Socket sendto returned " << *result
               << " which is less than the expected result " << payload.size();
  }

  return result == payload.size();
}

bool IcmpSession::TransmitV6EchoRequest(const net_base::IPv6Address& address) {
  struct icmp6_hdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  icmp_header.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header.icmp6_code = kIcmpEchoCode;
  icmp_header.icmp6_id = echo_id_;
  icmp_header.icmp6_seq = current_sequence_number_;
  const base::span<const uint8_t> payload = {
      reinterpret_cast<const uint8_t*>(&icmp_header), sizeof(icmp_header)};
  // icmp6_cksum is filled in by the kernel for IPPROTO_ICMPV6 sockets
  // (RFC3542 section 3.1)

  struct sockaddr_in6 destination_address;
  memset(&destination_address, 0, sizeof(destination_address));
  destination_address.sin6_family = AF_INET6;
  destination_address.sin6_scope_id = interface_index_;
  destination_address.sin6_addr = address.ToIn6Addr();

  const std::optional<size_t> result = socket_->SendTo(
      payload, 0, reinterpret_cast<struct sockaddr*>(&destination_address),
      sizeof(destination_address));
  if (!result) {
    PLOG(ERROR) << "Socket sendto failed";
  } else if (result < payload.size()) {
    LOG(ERROR) << "Socket sendto returned " << *result
               << " which is less than the expected result " << payload.size();
  }

  return result == payload.size();
}

int IcmpSession::OnV4EchoReplyReceived(base::span<const uint8_t> message) {
  if (message.size() < sizeof(struct iphdr)) {
    LOG(WARNING) << "Received ICMP packet is too short to contain IP header";
    return -1;
  }
  const struct iphdr* received_ip_header =
      reinterpret_cast<const struct iphdr*>(message.data());

  if (message.size() < received_ip_header->ihl * kIPHeaderLengthUnitBytes +
                           sizeof(struct icmphdr)) {
    LOG(WARNING) << "Received ICMP packet is too short to contain ICMP header";
    return -1;
  }
  const struct icmphdr* received_icmp_header =
      reinterpret_cast<const struct icmphdr*>(
          message.data() + received_ip_header->ihl * kIPHeaderLengthUnitBytes);

  // We might have received other types of ICMP traffic, so ensure that the
  // message is an echo reply before handling it.
  if (received_icmp_header->type != ICMP_ECHOREPLY) {
    return -1;
  }

  // Make sure the message is valid and matches a pending echo request.
  if (received_icmp_header->code != kIcmpEchoCode) {
    LOG(WARNING) << "ICMP header code is invalid";
    return -1;
  }

  if (received_icmp_header->un.echo.id != echo_id_) {
    SLOG(2) << "received message echo id (" << received_icmp_header->un.echo.id
            << ") does not match this ICMP session's echo id (" << echo_id_
            << ")";
    return -1;
  }

  return received_icmp_header->un.echo.sequence;
}

int IcmpSession::OnV6EchoReplyReceived(base::span<const uint8_t> message) {
  if (message.size() < sizeof(struct icmp6_hdr)) {
    LOG(WARNING)
        << "Received ICMP packet is too short to contain ICMPv6 header";
    return -1;
  }

  // Per RFC3542 section 3, ICMPv6 raw sockets do not contain the IP header
  // (unlike ICMPv4 raw sockets).
  const struct icmp6_hdr* received_icmp_header =
      reinterpret_cast<const struct icmp6_hdr*>(message.data());
  // We might have received other types of ICMP traffic, so ensure that the
  // message is an echo reply before handling it.
  if (received_icmp_header->icmp6_type != ICMP6_ECHO_REPLY) {
    return -1;
  }

  // Make sure the message is valid and matches a pending echo request.
  if (received_icmp_header->icmp6_code != kIcmpEchoCode) {
    LOG(WARNING) << "ICMPv6 header code is invalid";
    return -1;
  }

  if (received_icmp_header->icmp6_id != echo_id_) {
    SLOG(2) << "received message echo id (" << received_icmp_header->icmp6_id
            << ") does not match this ICMPv6 session's echo id (" << echo_id_
            << ")";
    return -1;
  }

  return received_icmp_header->icmp6_seq;
}

void IcmpSession::OnIcmpReadable() {
  std::vector<uint8_t> message;
  if (socket_->RecvMessage(&message)) {
    OnEchoReplyReceived(message);
  } else {
    PLOG(ERROR) << __func__ << ": failed to receive message from socket";
    // Do nothing when we encounter an IO error, so we can continue receiving
    // other pending echo replies.
  }
}

void IcmpSession::OnEchoReplyReceived(base::span<const uint8_t> message) {
  if (!destination_) {
    LOG(WARNING) << "Failed to get ICMP destination";
    return;
  }

  int received_seq_num = -1;
  switch (destination_->GetFamily()) {
    case net_base::IPFamily::kIPv4:
      received_seq_num = OnV4EchoReplyReceived(message);
      break;
    case net_base::IPFamily::kIPv6:
      received_seq_num = OnV6EchoReplyReceived(message);
      break;
  }

  if (received_seq_num < 0) {
    // Could not parse reply.
    return;
  }

  if (received_echo_reply_seq_numbers_.find(received_seq_num) !=
      received_echo_reply_seq_numbers_.end()) {
    // Echo reply for this message already handled previously.
    return;
  }

  const auto& seq_num_to_sent_recv_time_pair =
      seq_num_to_sent_recv_time_.find(received_seq_num);
  if (seq_num_to_sent_recv_time_pair == seq_num_to_sent_recv_time_.end()) {
    // Echo reply not meant for any sent echo requests.
    return;
  }

  // Record the time that the echo reply was received.
  seq_num_to_sent_recv_time_pair->second.second = base::TimeTicks::Now();
  received_echo_reply_seq_numbers_.insert(received_seq_num);

  if (received_echo_reply_seq_numbers_.size() == kTotalNumEchoRequests) {
    // All requests sent and replies received, so report results and end the
    // ICMP session.
    ReportResultAndStopSession();
  }
}

std::vector<base::TimeDelta> IcmpSession::GenerateIcmpResult() {
  std::vector<base::TimeDelta> latencies;
  for (const auto& seq_num_to_sent_recv_time_pair :
       seq_num_to_sent_recv_time_) {
    const SentRecvTimePair& sent_recv_timestamp_pair =
        seq_num_to_sent_recv_time_pair.second;
    if (sent_recv_timestamp_pair.second.is_null()) {
      // Invalid latency if an echo response has not been received.
      latencies.push_back(base::TimeDelta());
    } else {
      latencies.push_back(sent_recv_timestamp_pair.second -
                          sent_recv_timestamp_pair.first);
    }
  }
  return latencies;
}

void IcmpSession::ReportResultAndStopSession() {
  if (!IsStarted()) {
    LOG(WARNING) << "ICMP session not started";
    return;
  }
  Stop();
  // Invoke result callback after calling IcmpSession::Stop, since the callback
  // might delete this object. (Any subsequent call to IcmpSession::Stop leads
  // to a segfault since this function belongs to the deleted object.)
  if (!result_callback_.is_null()) {
    std::move(result_callback_).Run(GenerateIcmpResult());
  }
}

}  // namespace shill
