// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_
#define SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>

#include "shill/mockable.h"
#include "shill/network/icmp_session.h"

namespace shill {

class DnsClient;
class Error;
class EventDispatcher;

// Given a connected Network and a URL, ConnectionDiagnostics performs the
// following actions to diagnose a connectivity problem on the current
// Connection:
// (A) Start by pinging the IP address of the gateway.
// (B) Also starts by pinging all DNS servers in parallel
//     (C) Whether none or some of the DNS servers reply to ping,
//         try next resolve the IP of the target web server via DNS.
//         (D) IF DNS resolution succeeds, ping the IP address of the target
//             web server. END.
//         (E) Otherwise If DNS resolution fails for any other reason, END.
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
  enum class Result { kSuccess, kFailure, kTimeout, kPending };

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

  ConnectionDiagnostics(
      std::string_view iface_name,
      int iface_index,
      net_base::IPFamily ip_family,
      std::optional<net_base::IPAddress> gateway,
      const std::vector<net_base::IPAddress>& dns_list,
      std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
      std::string_view logging_tag,
      EventDispatcher* dispatcher);
  ConnectionDiagnostics(const ConnectionDiagnostics&) = delete;
  ConnectionDiagnostics& operator=(const ConnectionDiagnostics&) = delete;

  virtual ~ConnectionDiagnostics();

  // Performs connectivity diagnostics for the hostname of the URL |url|.
  mockable void Start(const net_base::HttpUrl& url);
  void Stop();
  mockable bool IsRunning() const;
  int event_number() const { return event_number_; }

  const std::string& interface_name() const { return iface_name_; }
  int interface_index() const { return iface_index_; }
  net_base::IPFamily ip_family() const { return ip_family_; }

 protected:
  EventDispatcher* get_dispatcher_for_testing() { return dispatcher_; }

  // Starts an ICMP session on |interface_index| to target |destination| and
  // returns the tracking IcmpSession object. Returns nullptr in case of error.
  mockable std::unique_ptr<IcmpSession> StartIcmpSession(
      const net_base::IPAddress& destination,
      int interface_index,
      std::string_view interface_name,
      IcmpSession::IcmpSessionResultCallback result_callback);

 private:
  friend class ConnectionDiagnosticsTest;

  // Buffers a diagnostic event with |type|, |result|, and an optional message,
  // and associates it with the given diagnostic id.
  void LogEvent(int diagnostic_id,
                Type type,
                Result result,
                const std::string& message = "");

  // Prints all buffered events in the order of their diagnostic ids.
  void PrintEvents();

  // Attempts to resolve the IP address of the hostname of |url| using
  // |dns_list|.
  void ResolveTargetServerIPAddress(
      const net_base::HttpUrl& url,
      const std::vector<net_base::IPAddress>& dns_list);

  // Starts ping diagnostics to all configured DNS servers of the current IP
  // family.
  void StartDNSServerPingDiagnostic();

  // Starts a ping diagnostic to the given address.
  void StartPingDiagnostic(Type type, const net_base::IPAddress& addr);

  // Starts a DNS and ping diagnostics for |url|.
  void StartHostDiagnostic(const net_base::HttpUrl& url);

  // Pings all the DNS servers of |dns_list_|.
  void PingDNSServers(int dns_diag_id);

  // Starts an IcmpSession with |address|. Called when we want to ping the
  // target web server or local gateway.
  void PingHost(int diagnostic_id,
                Type event_type,
                const net_base::IPAddress& address);

  // Called after each IcmpSession started in
  // ConnectionDiagnostics::PingDNSServers finishes or times out. The DNS server
  // that was pinged can be uniquely identified with |dns_server_index|.
  void OnPingDNSServerComplete(int diagnostic_id,
                               int dns_server_index,
                               const std::vector<base::TimeDelta>& result);

  // Called after the DNS IP address resolution on started in
  // ConnectionDiagnostics::ResolveTargetServerIPAddress completes.
  void OnDNSResolutionComplete(int diagnostic_id,
                               const net_base::IPAddress& dns,
                               const net_base::DNSClient::Result& result);

  // Called after the IcmpSession started in ConnectionDiagnostics::PingHost on
  // |address_pinged| finishes or times out. |ping_event_type| indicates the
  // type of ping that was started (gateway or target web server), and |result|
  // is the result of the IcmpSession.
  void OnPingHostComplete(int diagnostic_id,
                          Type ping_event_type,
                          const net_base::IPAddress& address_pinged,
                          const std::vector<base::TimeDelta>& result);

  void OnPingResult(int diagnostic_id,
                    Type ping_event_type,
                    const net_base::IPAddress& address_pinged,
                    const std::vector<base::TimeDelta>& result);

  // Assigns a new diagnostic id and populates a temporary log event with the
  // given type and default message.
  int AssignDiagnosticId(Type type, const std::string& default_message);
  // Clears a diagnostic id and remove any buffered log for that id.
  void ClearDiagnosticId(int diag_id);

  EventDispatcher* dispatcher_;
  std::unique_ptr<net_base::DNSClientFactory> dns_client_factory_;

  // The name of the network interface associated with the connection.
  std::string iface_name_;
  // The index of the network interface associated with the connection.
  int iface_index_;
  // The IP family used for all the diagnostics.
  net_base::IPFamily ip_family_;
  // The IP address of the gateway.
  std::optional<net_base::IPAddress> gateway_;
  std::vector<net_base::IPAddress> dns_list_;

  // All DNS queries resolving the host of |target_url_| currently in-flight,
  // keyed by the DNS server address.
  std::map<net_base::IPAddress, std::unique_ptr<net_base::DNSClient>>
      dns_queries_;
  // All addresses for the host of |target_url_| found with DNS queries so far.
  std::set<net_base::IPAddress> target_url_addresses_;

  // Used to ping multiple DNS servers in parallel.
  std::map<int, std::unique_ptr<IcmpSession>>
      id_to_pending_dns_server_icmp_session_;

  // All other ping requests currently in flight, tracked by the target
  // destination address.
  std::map<net_base::IPAddress, std::unique_ptr<IcmpSession>> icmp_sessions_;

  bool running_;

  // Number of record of all diagnostic events that occurred.
  int event_number_;

  std::string logging_tag_;

  int next_diagnostic_id_;
  std::map<int, Event> diagnostic_results_;

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
      std::optional<net_base::IPAddress> gateway,
      const std::vector<net_base::IPAddress>& dns_list,
      std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
      std::string_view logging_tag,
      EventDispatcher* dispatcher);
};

}  // namespace shill

#endif  // SHILL_NETWORK_CONNECTION_DIAGNOSTICS_H_
