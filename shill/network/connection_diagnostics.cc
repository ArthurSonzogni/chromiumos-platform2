// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/connection_diagnostics.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <chromeos/net-base/http_url.h>

#include "shill/dns_client.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/network/icmp_session.h"

namespace {
// These strings are dependent on ConnectionDiagnostics::Type. Any changes to
// this array should be synced with ConnectionDiagnostics::Type.
const char* const kEventNames[] = {"Portal detection", "Ping DNS servers",
                                   "DNS resolution", "Ping (target web server)",
                                   "Ping (gateway)"};
// These strings are dependent on ConnectionDiagnostics::Result. Any changes to
// this array should be synced with ConnectionDiagnostics::Result.
const char* const kResultNames[] = {"Success", "Failure", "Timeout"};

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

const int ConnectionDiagnostics::kMaxDNSRetries = 2;

ConnectionDiagnostics::ConnectionDiagnostics(
    std::string_view iface_name,
    int iface_index,
    const net_base::IPAddress& ip_address,
    const net_base::IPAddress& gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    EventDispatcher* dispatcher)
    : dispatcher_(dispatcher),
      iface_name_(iface_name),
      iface_index_(iface_index),
      ip_address_(ip_address),
      gateway_(gateway),
      dns_list_(dns_list),
      icmp_session_(new IcmpSession(dispatcher_)),
      num_dns_attempts_(0),
      running_(false),
      event_number_(0),
      weak_ptr_factory_(this) {
  dns_client_ = std::make_unique<DnsClient>(
      ip_address_.GetFamily(), iface_name, DnsClient::kDnsTimeout, dispatcher_,
      base::BindRepeating(&ConnectionDiagnostics::OnDNSResolutionComplete,
                          weak_ptr_factory_.GetWeakPtr()));
  for (size_t i = 0; i < dns_list_.size(); i++) {
    id_to_pending_dns_server_icmp_session_[i] =
        std::make_unique<IcmpSession>(dispatcher_);
  }
}

ConnectionDiagnostics::~ConnectionDiagnostics() {
  Stop();
}

bool ConnectionDiagnostics::Start(const net_base::HttpUrl& url) {
  if (running()) {
    LOG(ERROR) << iface_name_ << ": Diagnostics already started";
    return false;
  }

  LOG(INFO) << iface_name_ << ": Starting diagnostics for " << url.ToString();
  target_url_ = url;
  running_ = true;
  // Ping DNS servers to make sure at least one is reachable before resolving
  // the hostname of |target_url_|;
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&ConnectionDiagnostics::PingDNSServers,
                                       weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void ConnectionDiagnostics::Stop() {
  running_ = false;
  num_dns_attempts_ = 0;
  event_number_ = 0;
  dns_client_.reset();
  icmp_session_->Stop();
  id_to_pending_dns_server_icmp_session_.clear();
  target_url_ = std::nullopt;
}

// static
std::string ConnectionDiagnostics::EventToString(const Event& event) {
  auto message =
      base::StringPrintf("Event: %-26sResult: %-10s", kEventNames[event.type],
                         kResultNames[event.result]);
  if (!event.message.empty()) {
    message.append("Msg: " + event.message);
  }
  return message;
}

void ConnectionDiagnostics::LogEvent(Type type,
                                     Result result,
                                     const std::string& message) {
  event_number_++;
  Event ev(type, result, message);
  if (result == kResultSuccess) {
    LOG(INFO) << iface_name_ << ": Diagnostics event #" << event_number_ << ": "
              << EventToString(ev);
  } else {
    LOG(WARNING) << iface_name_ << ": Diagnostics event #" << event_number_
                 << ": " << EventToString(ev);
  }
}

void ConnectionDiagnostics::ResolveTargetServerIPAddress(
    const std::vector<std::string>& dns_list) {
  Error e;
  if (!dns_client_->Start(dns_list, target_url_->host(), &e)) {
    LogEvent(kTypeResolveTargetServerIP, kResultFailure,
             "Could not start DNS: " + e.message());
    Stop();
    return;
  }

  LogEvent(kTypeResolveTargetServerIP, kResultSuccess,
           base::StringPrintf("Attempt #%d", num_dns_attempts_));
  SLOG(2) << __func__ << ": looking up " << target_url_->host() << " (attempt "
          << num_dns_attempts_ << ")";
  ++num_dns_attempts_;
}

void ConnectionDiagnostics::PingDNSServers() {
  if (dns_list_.empty()) {
    LogEvent(kTypePingDNSServers, kResultFailure,
             "No DNS servers for this connection");
    Stop();
    return;
  }

  pingable_dns_servers_.clear();
  for (size_t i = 0; i < dns_list_.size(); ++i) {
    // If we encounter any errors starting ping for any DNS server, carry on
    // attempting to ping the other DNS servers rather than failing. We only
    // need to successfully ping a single DNS server to decide whether or not
    // DNS servers can be reached.
    const auto& dns_server_ip_addr = dns_list_[i];
    auto session_iter = id_to_pending_dns_server_icmp_session_.find(i);
    if (session_iter == id_to_pending_dns_server_icmp_session_.end()) {
      continue;
    }
    if (!session_iter->second->Start(
            dns_server_ip_addr, iface_index_, iface_name_,
            base::BindOnce(&ConnectionDiagnostics::OnPingDNSServerComplete,
                           weak_ptr_factory_.GetWeakPtr(), i))) {
      LogEvent(kTypePingDNSServers, kResultFailure,
               "Failed to initiate ping to DNS server " +
                   dns_server_ip_addr.ToString());
      id_to_pending_dns_server_icmp_session_.erase(i);
      continue;
    }

    SLOG(2) << __func__ << ": pinging DNS server at " << dns_server_ip_addr;
  }

  if (id_to_pending_dns_server_icmp_session_.empty()) {
    LogEvent(kTypePingDNSServers, kResultFailure,
             "Could not start ping for any of the given DNS servers");
    Stop();
    return;
  }

  LogEvent(kTypePingDNSServers, kResultSuccess);
}

void ConnectionDiagnostics::PingHost(const net_base::IPAddress& address) {
  SLOG(2) << __func__;

  const Type event_type =
      (address == gateway_) ? kTypePingGateway : kTypePingTargetServer;
  if (!icmp_session_->Start(
          address, iface_index_, iface_name_,
          base::BindOnce(&ConnectionDiagnostics::OnPingHostComplete,
                         weak_ptr_factory_.GetWeakPtr(), event_type,
                         address))) {
    LogEvent(event_type, kResultFailure,
             "Failed to start ICMP session with " + address.ToString());
    Stop();
    return;
  }

  LogEvent(event_type, kResultSuccess, "Pinging " + address.ToString());
}

void ConnectionDiagnostics::OnPingDNSServerComplete(
    int dns_server_index, const std::vector<base::TimeDelta>& result) {
  SLOG(2) << __func__ << "(DNS server index " << dns_server_index << ")";

  if (!id_to_pending_dns_server_icmp_session_.erase(dns_server_index)) {
    // This should not happen, since we expect exactly one callback for each
    // IcmpSession started with a unique |dns_server_index| value in
    // ConnectionDiagnostics::PingDNSServers. However, if this does happen for
    // any reason, |id_to_pending_dns_server_icmp_session_| might never become
    // empty, and we might never move to the next step after pinging DNS
    // servers. Stop diagnostics immediately to prevent this from happening.
    LogEvent(kTypePingDNSServers, kResultFailure,
             "No matching pending DNS server ICMP session found");
    Stop();
    return;
  }

  if (IcmpSession::AnyRepliesReceived(result)) {
    pingable_dns_servers_.push_back(dns_list_[dns_server_index].ToString());
  }
  if (!id_to_pending_dns_server_icmp_session_.empty()) {
    SLOG(2) << __func__ << ": not yet finished pinging all DNS servers";
    return;
  }

  if (pingable_dns_servers_.empty()) {
    LogEvent(kTypePingDNSServers, kResultFailure,
             "No DNS servers responded to pings. Pinging the gateway at " +
                 gateway_.ToString());
    // If no DNS servers can be pinged, try to ping the gateway.
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingHost,
                                  weak_ptr_factory_.GetWeakPtr(), gateway_));
    return;
  }

  if (pingable_dns_servers_.size() != dns_list_.size()) {
    LogEvent(kTypePingDNSServers, kResultSuccess,
             "Pinged some, but not all, DNS servers successfully");
  } else {
    LogEvent(kTypePingDNSServers, kResultSuccess,
             "Pinged all DNS servers successfully");
  }

  if (num_dns_attempts_ == kMaxDNSRetries) {
    LogEvent(kTypePingDNSServers, kResultFailure,
             "No DNS result after max DNS resolution attempts reached");
    Stop();
    return;
  }

  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(&ConnectionDiagnostics::ResolveTargetServerIPAddress,
                     weak_ptr_factory_.GetWeakPtr(), pingable_dns_servers_));
}

void ConnectionDiagnostics::OnDNSResolutionComplete(
    const base::expected<net_base::IPAddress, Error>& address) {
  SLOG(2) << __func__;

  if (address.has_value()) {
    LogEvent(kTypeResolveTargetServerIP, kResultSuccess,
             "Target address is " + address->ToString());
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingHost,
                                  weak_ptr_factory_.GetWeakPtr(), *address));
  } else if (address.error().type() == Error::kOperationTimeout) {
    LogEvent(kTypeResolveTargetServerIP, kResultTimeout,
             "DNS resolution timed out: " + address.error().message());
    dispatcher_->PostTask(FROM_HERE,
                          base::BindOnce(&ConnectionDiagnostics::PingDNSServers,
                                         weak_ptr_factory_.GetWeakPtr()));
  } else {
    LogEvent(kTypeResolveTargetServerIP, kResultFailure,
             "DNS resolution failed: " + address.error().message());
    Stop();
  }
}

void ConnectionDiagnostics::OnPingHostComplete(
    Type ping_event_type,
    const net_base::IPAddress& address_pinged,
    const std::vector<base::TimeDelta>& result) {
  SLOG(2) << __func__;

  auto message = base::StringPrintf("Destination: %s,  Latencies: ",
                                    address_pinged.ToString().c_str());
  for (const auto& latency : result) {
    if (latency.is_zero()) {
      message.append("NA ");
    } else {
      message.append(base::StringPrintf("%4.2fms ", latency.InMillisecondsF()));
    }
  }

  Result result_type =
      IcmpSession::AnyRepliesReceived(result) ? kResultSuccess : kResultFailure;
  LogEvent(ping_event_type, result_type, message);
  if (result_type == kResultFailure &&
      ping_event_type == kTypePingTargetServer) {
    // If pinging the target web server fails, try pinging the gateway.
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingHost,
                                  weak_ptr_factory_.GetWeakPtr(), gateway_));
  } else {
    // Otherwise stop
    Stop();
  }
}

std::unique_ptr<ConnectionDiagnostics> ConnectionDiagnosticsFactory::Create(
    std::string_view iface_name,
    int iface_index,
    const net_base::IPAddress& ip_address,
    const net_base::IPAddress& gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    EventDispatcher* dispatcher) {
  return std::make_unique<ConnectionDiagnostics>(
      iface_name, iface_index, ip_address, gateway, dns_list, dispatcher);
}

}  // namespace shill
