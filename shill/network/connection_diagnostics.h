// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_
#define SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>

#include "shill/mockable.h"

namespace shill {

class DnsClient;
class Error;
class EventDispatcher;
class IcmpSession;
class IcmpSessionFactory;

// Given a connected Network and a URL, ConnectionDiagnostics performs the
// following actions to diagnose a connectivity problem on the current
// Connection:
// (A) Starts by pinging all DNS servers.
//     (B) If none of the DNS servers reply to pings, then we might have a
//         problem reaching DNS servers. Check if the gateway can be pinged
//         (step I).
//     (C) If at least one DNS server replies to pings but we are out of DNS
//         retries, the DNS servers are at fault. END.
//     (D) If at least one DNS server replies to pings, and we have DNS
//         retries left, resolve the IP of the target web server via DNS.
//         (E) If DNS resolution fails because of a timeout, ping all DNS
//             servers again and find a new reachable DNS server (step A).
//         (F) If DNS resolution fails for any other reason, we have found a
//             DNS server issue. END.
//         (G) Otherwise, ping the IP address of the target web server.
//             (H) If ping is successful, we can reach the target web server. We
//                 might have a HTTP issue or a broken portal. END.
//             (I) If ping is unsuccessful, ping the IP address of the gateway.
//                 (J) If the local gateway respond to pings, then we have
//                     found an upstream connectivity problem or gateway
//                     problem. END.
//                 (K) If there is no response, then the local gateway may not
//                     be responding to pings, or it may not exist on the local
//                     network or be unreachable if there are link layer issues.
//                     END.
//
// TODO(samueltan): Step F: if retry succeeds, remove the unresponsive DNS
// servers so Chrome does not try to use them.
class ConnectionDiagnostics {
 public:
  // Describes the type of a diagnostic test.
  enum class Type {
    kPingDNSServers,
    kResolveTargetServerIP,
    kPingTargetServer,
    kPingGateway,
  };

  // Describes the result of a diagnostic test.
  enum class Result { kSuccess, kFailure, kTimeout };

  struct Event {
    Event(Type type_in, Result result_in, const std::string& message_in)
        : type(type_in), result(result_in), message(message_in) {}
    Type type;
    Result result;
    std::string message;
  };

  // Returns the string name of |type|.
  static std::string TypeName(Type type);
  // Returns the string name of |result|.
  static std::string ResultName(Result result);
  // Returns a string representation of |event|.
  static std::string EventToString(const Event& event);

  ConnectionDiagnostics(std::string_view iface_name,
                        int iface_index,
                        net_base::IPFamily ip_family,
                        const net_base::IPAddress& gateway,
                        const std::vector<net_base::IPAddress>& dns_list,
                        std::string_view logging_tag,
                        EventDispatcher* dispatcher);
  ConnectionDiagnostics(const ConnectionDiagnostics&) = delete;
  ConnectionDiagnostics& operator=(const ConnectionDiagnostics&) = delete;

  virtual ~ConnectionDiagnostics();

  // Performs connectivity diagnostics for the hostname of the URL |url|.
  mockable bool Start(const net_base::HttpUrl& url);
  void Stop();
  mockable bool IsRunning() const;
  int event_number() const { return event_number_; }

 private:
  friend class ConnectionDiagnosticsTest;

  static const int kMaxDNSRetries;

  // Logs a diagnostic events with |type|, |result|, and an optional message.
  void LogEvent(Type type, Result result, const std::string& message = "");

  // Attempts to resolve the IP address of the hostname of |target_url_| using
  // |dns_list|.
  void ResolveTargetServerIPAddress(const std::vector<std::string>& dns_list);

  // Pings all the DNS servers of |dns_list_|.
  void PingDNSServers();

  // Starts an IcmpSession with |address|. Called when we want to ping the
  // target web server or local gateway.
  void PingHost(const net_base::IPAddress& address);

  // Called after each IcmpSession started in
  // ConnectionDiagnostics::PingDNSServers finishes or times out. The DNS server
  // that was pinged can be uniquely identified with |dns_server_index|.
  // Attempts to resolve the IP address of the hostname of |target_url_| again
  // if at least one DNS server was pinged successfully, and if
  // |num_dns_attempts_| has not yet reached |kMaxDNSRetries|.
  void OnPingDNSServerComplete(int dns_server_index,
                               const std::vector<base::TimeDelta>& result);

  // Called after the DNS IP address resolution on started in
  // ConnectionDiagnostics::ResolveTargetServerIPAddress completes.
  void OnDNSResolutionComplete(
      const base::expected<net_base::IPAddress, Error>& address);

  // Called after the IcmpSession started in ConnectionDiagnostics::PingHost on
  // |address_pinged| finishes or times out. |ping_event_type| indicates the
  // type of ping that was started (gateway or target web server), and |result|
  // is the result of the IcmpSession.
  void OnPingHostComplete(Type ping_event_type,
                          const net_base::IPAddress& address_pinged,
                          const std::vector<base::TimeDelta>& result);

  EventDispatcher* dispatcher_;

  // The name of the network interface associated with the connection.
  std::string iface_name_;
  // The index of the network interface associated with the connection.
  int iface_index_;
  // The IP family used for all the diagnostics.
  net_base::IPFamily ip_family_;
  // The IP address of the gateway.
  net_base::IPAddress gateway_;
  std::vector<net_base::IPAddress> dns_list_;

  // TODO(b/307880493): Migrate to net_base::DNSClient.
  std::unique_ptr<DnsClient> dns_client_;
  std::unique_ptr<IcmpSession> icmp_session_;

  // The URL whose hostname is being diagnosed. Only defined when the
  // diagnostics is running.
  std::optional<net_base::HttpUrl> target_url_;

  // Used to ping multiple DNS servers in parallel.
  std::map<int, std::unique_ptr<IcmpSession>>
      id_to_pending_dns_server_icmp_session_;
  // TODO(b/307880493): Migrate to net_base::DNSClient and avoid
  // converting the pingable net_base::IPAddress values to std::string.
  std::vector<std::string> pingable_dns_servers_;

  int num_dns_attempts_;
  bool running_;

  // Number of record of all diagnostic events that occurred.
  int event_number_;

  std::string logging_tag_;

  base::WeakPtrFactory<ConnectionDiagnostics> weak_ptr_factory_;
};

// The factory class of the ConnectionDiagnostics, used to derive a mock factory
// to create mock ConnectionDiagnostics instance at testing.
class ConnectionDiagnosticsFactory {
 public:
  ConnectionDiagnosticsFactory() = default;
  virtual ~ConnectionDiagnosticsFactory() = default;

  // The default factory method, calling ConnectionDiagnostics's constructor.
  mockable std::unique_ptr<ConnectionDiagnostics> Create(
      std::string_view iface_name,
      int iface_index,
      net_base::IPFamily ip_family,
      const net_base::IPAddress& gateway,
      const std::vector<net_base::IPAddress>& dns_list,
      std::string_view logging_tag,
      EventDispatcher* dispatcher);
};

}  // namespace shill

#endif  // SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_
