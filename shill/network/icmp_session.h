// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_ICMP_SESSION_H_
#define SHILL_NETWORK_ICMP_SESSION_H_

#include <netinet/ip_icmp.h>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/containers/span.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/socket.h>

#include "shill/event_dispatcher.h"

namespace shill {

// The IcmpSession class encapsulates the task of performing a stateful exchange
// of echo requests and echo replies between this host and another (i.e. ping).
// Each IcmpSession object only allows one ICMP sequence of requests to be
// running at one time. Multiple ICMP sessions can be run concurrently by
// creating multiple IcmpSession objects.
class IcmpSession {
 public:
  // The number of echo requests sent by one session.
  static constexpr int kTotalNumEchoRequests = 3;
  // The interval between each echo request. Default for ping.
  static constexpr base::TimeDelta kEchoRequestInterval = base::Seconds(1);
  // The ICMP echo code, specified in RFC 792.
  static constexpr int kIcmpEchoCode = 0;
  // The timeout of the session. We should not need more than 1 second after the
  // last request is sent to receive the final reply.
  static constexpr base::TimeDelta kTimeout =
      kEchoRequestInterval * kTotalNumEchoRequests + base::Seconds(1);

  // The result of an ICMP session is a vector of time deltas representing how
  // long it took to receive a echo reply for each sent echo request. The vector
  // is sorted in the order that the echo requests were sent. Zero time deltas
  // represent echo requests that we did not receive a corresponding reply for.
  using IcmpSessionResult = std::vector<base::TimeDelta>;
  using IcmpSessionResultCallback =
      base::OnceCallback<void(const IcmpSessionResult&)>;

  // Creates a instance and override the echo ID, only used for testing.
  static std::unique_ptr<IcmpSession> CreateForTesting(
      EventDispatcher* dispatcher,
      std::unique_ptr<net_base::SocketFactory> socket_factory,
      int echo_id);

  explicit IcmpSession(EventDispatcher* dispatcher,
                       std::unique_ptr<net_base::SocketFactory> socket_factory =
                           std::make_unique<net_base::SocketFactory>());
  IcmpSession(const IcmpSession&) = delete;
  IcmpSession& operator=(const IcmpSession&) = delete;

  // We always call IcmpSession::Stop in the destructor to clean up, in case an
  // ICMP session is still in progress.
  virtual ~IcmpSession();

  // Starts an ICMP session, sending |kNumEchoRequestsToSend| echo requests to
  // |destination| via the network interface |interface_name|,
  // |kEchoRequestInterval| apart. |result_callback| will be called a) after all
  // echo requests are sent and all echo replies are received, or b) after
  // |kTimeout| have passed. |result_callback| will only be invoked once on the
  // first occurrence of either of these events. |interface_index| is the IPv6
  // scope ID, which can be 0 for a global |destination| but must be the value
  // matching |interface_name| if |destination| is a link-local address. It is
  // unused on IPv4.
  virtual bool Start(const net_base::IPAddress& destination,
                     int interface_index,
                     std::string_view interface_name,
                     IcmpSessionResultCallback result_callback);

  // Stops the current ICMP session by closing the ICMP socket and resetting
  // callbacks. Does nothing if a ICMP session is not started.
  virtual void Stop();

  // Returns true if this ICMP session has started, or false otherwise.
  bool IsStarted() const;

  // Utility function that returns false iff |result| indicates that no echo
  // replies were received to any ICMP echo request that was sent during the
  // ICMP session that generated |result|.
  static bool AnyRepliesReceived(const IcmpSessionResult& result);

  // Utility function that returns the packet loss rate for the ICMP session
  // that generated |result| is greater than |percentage_threshold| percent.
  // The percentage packet loss determined by this function will be rounded
  // down to the closest integer percentage value. |percentage_threshold| is
  // expected to be a non-negative integer value.
  static bool IsPacketLossPercentageGreaterThan(const IcmpSessionResult& result,
                                                int percentage_threshold);

  // Computes the checksum for Echo Request |hdr| of length |len| according
  // to specifications in RFC 792.
  static uint16_t ComputeIcmpChecksum(const struct icmphdr& hdr, size_t len);

  // Accesses the private data or method for testing.
  int GetEchoIdForTesting() const { return echo_id_; }
  void OnEchoReplyReceivedForTesting(base::span<const uint8_t> message) {
    OnEchoReplyReceived(message);
  }

 private:
  using SentRecvTimePair = std::pair<base::TimeTicks, base::TimeTicks>;

  static uint16_t kNextUniqueEchoId;  // unique across IcmpSession objects

  // Sends a single echo request to the destination. This function will call
  // itself repeatedly via the event loop every |kEchoRequestInterval|
  // until |kNumEchoRequestToSend| echo requests are sent or the timeout is
  // reached.
  void TransmitEchoRequestTask();

  // Sends an ICMP Echo Request (Ping) packet to |address| in IPv4 and IPv6.
  bool TransmitV4EchoRequest(const net_base::IPv4Address& address);
  bool TransmitV6EchoRequest(const net_base::IPv6Address& address);

  // Called by |icmp_->socket()| when the ICMP socket is ready to read.
  void OnIcmpReadable();

  // Called when an ICMP packet is received.
  void OnEchoReplyReceived(base::span<const uint8_t> message);

  // IPv4 and IPv6 packet parsers.
  int OnV4EchoReplyReceived(base::span<const uint8_t> message);
  int OnV6EchoReplyReceived(base::span<const uint8_t> message);

  // Helper function that generates the result of the current ICMP session.
  IcmpSessionResult GenerateIcmpResult();

  // Calls |result_callback_| with the results collected so far, then stops the
  // IcmpSession. This function is called when the ICMP session successfully
  // completes, or when it times out. Does nothing if an ICMP session is not
  // started.
  void ReportResultAndStopSession();

  EventDispatcher* dispatcher_;

  std::unique_ptr<net_base::SocketFactory> socket_factory_;
  std::unique_ptr<net_base::Socket> socket_;
  std::optional<net_base::IPAddress> destination_;
  int interface_index_ = -1;

  // unique ID for this object's echo request/replies
  uint16_t echo_id_;
  uint16_t current_sequence_number_ = 0;
  std::map<uint16_t, SentRecvTimePair> seq_num_to_sent_recv_time_;
  std::set<uint16_t> received_echo_reply_seq_numbers_;
  base::CancelableOnceClosure timeout_callback_;
  IcmpSessionResultCallback result_callback_;

  base::WeakPtrFactory<IcmpSession> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_ICMP_SESSION_H_
