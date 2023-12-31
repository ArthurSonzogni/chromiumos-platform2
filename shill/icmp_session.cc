// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/icmp_session.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>

#include <base/check_op.h>
#include <base/containers/span.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/time/default_tick_clock.h>
#include <base/time/time.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace {
const int kIPHeaderLengthUnitBytes = 4;
}

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

uint16_t IcmpSession::kNextUniqueEchoId = 0;

IcmpSession::IcmpSession(EventDispatcher* dispatcher)
    : weak_ptr_factory_(this),
      dispatcher_(dispatcher),
      icmp_(new Icmp()),
      echo_id_(kNextUniqueEchoId),
      current_sequence_number_(0),
      tick_clock_(&default_tick_clock_) {
  // Each IcmpSession will have a unique echo ID to identify requests and reply
  // messages.
  ++kNextUniqueEchoId;
}

IcmpSession::~IcmpSession() {
  Stop();
}

bool IcmpSession::Start(const net_base::IPAddress& destination,
                        int interface_index,
                        IcmpSessionResultCallback result_callback) {
  if (!dispatcher_) {
    LOG(ERROR) << "Invalid dispatcher";
    return false;
  }
  if (IsStarted()) {
    LOG(WARNING) << "ICMP session already started";
    return false;
  }
  if (!icmp_->Start(destination, interface_index)) {
    return false;
  }

  icmp_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      icmp_->socket()->Get(), base::BindRepeating(&IcmpSession::OnIcmpReadable,
                                                  base::Unretained(this)));
  result_callback_ = std::move(result_callback);
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
  icmp_watcher_.reset();
  icmp_->Stop();
}

bool IcmpSession::IsStarted() const {
  return icmp_->IsStarted();
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

void IcmpSession::TransmitEchoRequestTask() {
  if (!IsStarted()) {
    // This might happen when ping times out or is stopped between two calls
    // to IcmpSession::TransmitEchoRequestTask.
    return;
  }
  if (icmp_->TransmitEchoRequest(echo_id_, current_sequence_number_)) {
    seq_num_to_sent_recv_time_[current_sequence_number_] =
        std::make_pair(tick_clock_->NowTicks(), base::TimeTicks());
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
  if (received_icmp_header->code != Icmp::kIcmpEchoCode) {
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
  if (received_icmp_header->icmp6_code != Icmp::kIcmpEchoCode) {
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
  if (icmp_->socket()->RecvMessage(&message)) {
    OnEchoReplyReceived(message);
  } else {
    PLOG(ERROR) << __func__ << ": failed to receive message from socket";
    // Do nothing when we encounter an IO error, so we can continue receiving
    // other pending echo replies.
  }
}

void IcmpSession::OnEchoReplyReceived(base::span<const uint8_t> message) {
  const auto& destination = icmp_->destination();
  if (!destination) {
    LOG(WARNING) << "Failed to get ICMP destination";
    return;
  }

  int received_seq_num = -1;
  switch (destination->GetFamily()) {
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
  seq_num_to_sent_recv_time_pair->second.second = tick_clock_->NowTicks();
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
