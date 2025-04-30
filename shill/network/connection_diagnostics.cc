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
#include <base/strings/strcat.h>
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
// These strings are dependent on ConnectionDiagnostics::Result. Any changes to
// this array should be synced with ConnectionDiagnostics::Result.

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

// static
std::string ConnectionDiagnostics::TypeName(Type type) {
  switch (type) {
    case Type::kPingDNSServers:
      return "Ping DNS servers";
    case Type::kResolveTargetServerIP:
      return "DNS resolution";
    case Type::kPingTargetServer:
      return "Ping (target web server)";
    case Type::kPingGateway:
      return "Ping (gateway)";
  }
}

// static
std::string ConnectionDiagnostics::ResultName(Result result) {
  switch (result) {
    case Result::kSuccess:
      return "Success";
    case Result::kFailure:
      return "Failure";
    case Result::kTimeout:
      return "Timeout";
    case Result::kPending:
      return "Pending";
  }
}

ConnectionDiagnostics::ConnectionDiagnostics(
    std::string_view iface_name,
    int iface_index,
    net_base::IPFamily ip_family,
    const net_base::IPAddress& gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    std::string_view logging_tag,
    EventDispatcher* dispatcher)
    : dispatcher_(dispatcher),
      iface_name_(iface_name),
      iface_index_(iface_index),
      ip_family_(ip_family),
      gateway_(gateway),
      dns_resolution_diagnostic_id_(0),
      icmp_session_(new IcmpSession(dispatcher_)),
      running_(false),
      event_number_(0),
      logging_tag_(logging_tag),
      next_diagnostic_id_(0),
      weak_ptr_factory_(this) {
  dns_client_ = std::make_unique<DnsClient>(
      ip_family_, iface_name, DnsClient::kDnsTimeout, dispatcher_,
      base::BindRepeating(&ConnectionDiagnostics::OnDNSResolutionComplete,
                          weak_ptr_factory_.GetWeakPtr()));
  for (const auto& dns : dns_list) {
    if (dns.GetFamily() == ip_family) {
      dns_list_.push_back(dns);
    }
  }
}

ConnectionDiagnostics::~ConnectionDiagnostics() {
  Stop();
}

void ConnectionDiagnostics::Start(const net_base::HttpUrl& url) {
  if (IsRunning()) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": " << ip_family_
               << " Diagnostics already started";
    return;
  }

  LOG(INFO) << logging_tag_ << " " << __func__ << ": Starting " << ip_family_
            << " diagnostics for " << url.ToString();
  target_url_ = url;
  running_ = true;

  // Always ping the gateway.
  StartPingDiagnostic(Type::kPingGateway, gateway_);

  // Ping DNS servers to make sure at least one is reachable before resolving
  // the hostname of |target_url_|;
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&ConnectionDiagnostics::PingDNSServers,
                                       weak_ptr_factory_.GetWeakPtr()));
}

void ConnectionDiagnostics::Stop() {
  PrintEvents();
  LOG(INFO) << logging_tag_ << " " << __func__ << ": Stopping " << ip_family_
            << " diagnostics";
  running_ = false;
  event_number_ = 0;
  dns_client_.reset();
  icmp_session_->Stop();
  id_to_pending_dns_server_icmp_session_.clear();
  target_url_ = std::nullopt;
}

bool ConnectionDiagnostics::IsRunning() const {
  return running_;
}

// static
std::string ConnectionDiagnostics::EventToString(const Event& event) {
  auto message =
      base::StrCat({TypeName(event.type), ": ", ResultName(event.result)});
  if (!event.message.empty()) {
    base::StrAppend(&message, {", ", event.message});
  }
  return message;
}

void ConnectionDiagnostics::LogEvent(int diagnostic_id,
                                     Type type,
                                     Result result,
                                     const std::string& message) {
  event_number_++;
  Event ev(type, result, message);
  auto [it, success] = diagnostic_results_.insert({diagnostic_id, ev});
  if (!success) {
    it->second = ev;
  }
}

void ConnectionDiagnostics::PrintEvents() {
  if (!running_) {
    return;
  }
  for (const auto& [id, ev] : diagnostic_results_) {
    if (ev.result == Result::kSuccess) {
      LOG(INFO) << logging_tag_ << " " << __func__ << ": " << ip_family_ << " "
                << EventToString(ev);
    } else {
      LOG(WARNING) << logging_tag_ << " " << __func__ << ": " << ip_family_
                   << " " << EventToString(ev);
    }
  }
  next_diagnostic_id_ = 0;
  diagnostic_results_.clear();
}

void ConnectionDiagnostics::ResolveTargetServerIPAddress(
    const std::vector<net_base::IPAddress>& dns_list) {
  const std::string& host = target_url_->host();
  dns_resolution_diagnostic_id_ =
      AssignDiagnosticId(Type::kResolveTargetServerIP, "Resolving " + host);

  std::vector<std::string> dns;
  for (const auto& addr : dns_list) {
    dns.push_back(addr.ToString());
  }

  Error e;
  if (!dns_client_->Start(dns, host, &e)) {
    LogEvent(dns_resolution_diagnostic_id_, Type::kResolveTargetServerIP,
             Result::kFailure, "Could not start DNS: " + e.message());
    Stop();
    return;
  }

  SLOG(2) << logging_tag_ << " " << __func__ << ": looking up "
          << target_url_->host();
}

void ConnectionDiagnostics::PingDNSServers() {
  // get id for this operation
  int dns_diag_id =
      AssignDiagnosticId(Type::kPingDNSServers, "Ping DNS servers");

  if (dns_list_.empty()) {
    LogEvent(dns_diag_id, Type::kPingDNSServers, Result::kFailure,
             "No DNS servers for this connection");
    Stop();
    return;
  }

  for (size_t i = 0; i < dns_list_.size(); ++i) {
    const auto& dns_server_ip_addr = dns_list_[i];
    int diag_id = AssignDiagnosticId(
        Type::kPingDNSServers, "Pinging " + dns_server_ip_addr.ToString());
    auto icmp_session = StartIcmpSession(
        dns_server_ip_addr, iface_index_, iface_name_,
        base::BindOnce(&ConnectionDiagnostics::OnPingDNSServerComplete,
                       weak_ptr_factory_.GetWeakPtr(), diag_id, i));
    if (!icmp_session) {
      // If we encounter any errors starting ping for any DNS server, carry on
      // attempting to ping the other DNS servers rather than failing.
      LogEvent(diag_id, Type::kPingDNSServers, Result::kFailure,
               "Failed to initiate ping to DNS server " +
                   dns_server_ip_addr.ToString());
      continue;
    }

    id_to_pending_dns_server_icmp_session_[i] = std::move(icmp_session);
    SLOG(2) << logging_tag_ << " " << __func__ << ": pinging DNS server at "
            << dns_server_ip_addr;
  }

  if (id_to_pending_dns_server_icmp_session_.empty()) {
    LogEvent(dns_diag_id, Type::kPingDNSServers, Result::kFailure,
             "Could not start ping for any of the given DNS servers");
    Stop();
  } else {
    ClearDiagnosticId(dns_diag_id);
  }
}

void ConnectionDiagnostics::PingHost(int diagnostic_id,
                                     Type event_type,
                                     const net_base::IPAddress& address) {
  SLOG(2) << logging_tag_ << " " << __func__;
  if (!icmp_session_->Start(
          address, iface_index_, iface_name_,
          base::BindOnce(&ConnectionDiagnostics::OnPingHostComplete,
                         weak_ptr_factory_.GetWeakPtr(), diagnostic_id,
                         event_type, address))) {
    LogEvent(diagnostic_id, event_type, Result::kFailure,
             "Failed to start ICMP session with " + address.ToString());
    Stop();
  }
}

void ConnectionDiagnostics::OnPingDNSServerComplete(
    int diagnostic_id,
    int dns_server_index,
    const std::vector<base::TimeDelta>& result) {
  SLOG(2) << logging_tag_ << " " << __func__ << ": DNS server index "
          << dns_server_index;

  if (!id_to_pending_dns_server_icmp_session_.erase(dns_server_index)) {
    // This should not happen, since we expect exactly one callback for each
    // IcmpSession started with a unique |dns_server_index| value in
    // ConnectionDiagnostics::PingDNSServers. However, if this does happen for
    // any reason, |id_to_pending_dns_server_icmp_session_| might never become
    // empty, and we might never move to the next step after pinging DNS
    // servers. Stop diagnostics immediately to prevent this from happening.
    LogEvent(diagnostic_id, Type::kPingDNSServers, Result::kFailure,
             "No matching pending DNS server ICMP session found");
    Stop();
    return;
  }

  OnPingResult(diagnostic_id, Type::kPingDNSServers,
               dns_list_[dns_server_index], result);

  if (!id_to_pending_dns_server_icmp_session_.empty()) {
    SLOG(2) << logging_tag_ << " " << __func__
            << ": not yet finished pinging all DNS servers";
    return;
  }

  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(&ConnectionDiagnostics::ResolveTargetServerIPAddress,
                     weak_ptr_factory_.GetWeakPtr(), dns_list_));
}

void ConnectionDiagnostics::OnDNSResolutionComplete(
    const base::expected<net_base::IPAddress, Error>& address) {
  SLOG(2) << logging_tag_ << " " << __func__;
  if (address.has_value()) {
    LogEvent(dns_resolution_diagnostic_id_, Type::kResolveTargetServerIP,
             Result::kSuccess, "Target address is " + address->ToString());
    StartPingDiagnostic(Type::kPingTargetServer, *address);
  } else {
    LogEvent(dns_resolution_diagnostic_id_, Type::kResolveTargetServerIP,
             Result::kFailure, address.error().message());
    Stop();
  }
}

void ConnectionDiagnostics::OnPingHostComplete(
    int diagnostic_id,
    Type event_type,
    const net_base::IPAddress& address_pinged,
    const std::vector<base::TimeDelta>& result) {
  SLOG(2) << logging_tag_ << " " << __func__;

  OnPingResult(diagnostic_id, event_type, address_pinged, result);

  // Pinging the target server is the last operation.
  if (event_type == Type::kPingTargetServer) {
    Stop();
  }
}

void ConnectionDiagnostics::OnPingResult(
    int diagnostic_id,
    Type event_type,
    const net_base::IPAddress& address_pinged,
    const std::vector<base::TimeDelta>& result) {
  std::string message = base::StrCat({"Pinged ", address_pinged.ToString()});
  std::string sep = ": ";
  for (const auto& latency : result) {
    message.append(sep);
    if (!latency.is_zero()) {
      message.append(base::StringPrintf("%4.2fms", latency.InMillisecondsF()));
    } else {
      message.append("NA");
    }
    sep = ", ";
  }

  Result result_type = IcmpSession::AnyRepliesReceived(result)
                           ? Result::kSuccess
                           : Result::kFailure;
  LogEvent(diagnostic_id, event_type, result_type, message);
}

std::unique_ptr<ConnectionDiagnostics> ConnectionDiagnosticsFactory::Create(
    std::string_view iface_name,
    int iface_index,
    net_base::IPFamily ip_family,
    const net_base::IPAddress& gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    std::string_view logging_tag,
    EventDispatcher* dispatcher) {
  return std::make_unique<ConnectionDiagnostics>(iface_name, iface_index,
                                                 ip_family, gateway, dns_list,
                                                 logging_tag, dispatcher);
}

std::unique_ptr<IcmpSession> ConnectionDiagnostics::StartIcmpSession(
    const net_base::IPAddress& destination,
    int interface_index,
    std::string_view interface_name,
    IcmpSession::IcmpSessionResultCallback result_callback) {
  auto icmp_session = std::make_unique<IcmpSession>(dispatcher_);
  if (!icmp_session->Start(destination, interface_index, interface_name,
                           std::move(result_callback))) {
    return nullptr;
  }
  return icmp_session;
}

int ConnectionDiagnostics::AssignDiagnosticId(
    Type type, const std::string& default_message) {
  int id = next_diagnostic_id_++;
  diagnostic_results_.insert(
      {id, Event(type, Result::kPending, default_message)});
  return id;
}

void ConnectionDiagnostics::ClearDiagnosticId(int diag_id) {
  diagnostic_results_.erase(diag_id);
}

void ConnectionDiagnostics::StartPingDiagnostic(
    Type type, const net_base::IPAddress& addr) {
  int diagnostic_id = AssignDiagnosticId(type, "Pinging " + addr.ToString());
  dispatcher_->PostTask(
      FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingHost,
                                weak_ptr_factory_.GetWeakPtr(), diagnostic_id,
                                type, addr));
}

}  // namespace shill
