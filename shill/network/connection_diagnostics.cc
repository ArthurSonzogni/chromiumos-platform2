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
#include <base/time/time.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>

#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/network/icmp_session.h"

namespace {
// Maximum number of query tries per name server.
constexpr int kDNSNumberOfQueries = 2;
// Timeout of a single query to a single name server.
constexpr base::TimeDelta kDNSTimeoutOfQueries = base::Seconds(2);
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
    std::optional<net_base::IPAddress> gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
    std::unique_ptr<IcmpSessionFactory> icmp_session_factory,
    std::string_view logging_tag,
    EventDispatcher* dispatcher)
    : dispatcher_(dispatcher),
      dns_client_factory_(std::move(dns_client_factory)),
      icmp_session_factory_(std::move(icmp_session_factory)),
      iface_name_(iface_name),
      iface_index_(iface_index),
      ip_family_(ip_family),
      gateway_(gateway),
      logging_tag_(logging_tag),
      next_diagnostic_id_(0),
      weak_ptr_factory_(this) {
  for (const auto& dns : dns_list) {
    if (dns.GetFamily() == ip_family) {
      dns_list_.push_back(dns);
    }
  }
}

ConnectionDiagnostics::~ConnectionDiagnostics() {
  if (IsRunning()) {
    Stop();
  }
}

void ConnectionDiagnostics::Start(const net_base::HttpUrl& url) {
  if (IsRunning()) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": " << ip_family_
               << " Diagnostics already started";
    return;
  }

  LOG(INFO) << logging_tag_ << " " << __func__ << ": Starting " << ip_family_
            << " diagnostics for " << url.ToString();

  StartGatewayPingDiagnostic();
  StartDNSServerPingDiagnostic();
  StartHostDiagnostic(url);
}

void ConnectionDiagnostics::Stop() {
  PrintEvents();
  LOG(INFO) << logging_tag_ << " " << __func__ << ": Stopping " << ip_family_
            << " diagnostics";
  dns_queries_.clear();
  target_url_addresses_.clear();
  id_to_pending_dns_server_icmp_session_.clear();
  host_icmp_sessions_.clear();
  gateway_icmp_session_ = nullptr;
  gateway_ping_running_ = false;
  dns_ping_running_ = false;
  host_resolution_running_ = false;
  host_ping_running_ = false;
}

bool ConnectionDiagnostics::IsRunning() const {
  return gateway_ping_running_ || dns_ping_running_ ||
         host_resolution_running_ || host_ping_running_;
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
  Event ev(type, result, message);
  auto [it, success] = diagnostic_results_.insert({diagnostic_id, ev});
  if (!success) {
    it->second = ev;
  }
  if (!IsRunning()) {
    Stop();
  }
}

void ConnectionDiagnostics::PrintEvents() {
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

void ConnectionDiagnostics::StartHostDiagnostic(const net_base::HttpUrl& url) {
  host_resolution_running_ = true;
  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(&ConnectionDiagnostics::ResolveHostIPAddress,
                     weak_ptr_factory_.GetWeakPtr(), url, dns_list_));
}

void ConnectionDiagnostics::ResolveHostIPAddress(
    const net_base::HttpUrl& url,
    const std::vector<net_base::IPAddress>& dns_list) {
  if (dns_list_.empty()) {
    host_resolution_running_ = false;
    LogEvent(AssignDiagnosticId(Type::kPingTargetServer,
                                "Pinging " + url.ToString()),
             Type::kPingTargetServer, Result::kSuccess,
             "Skipped because DNS servers are not defined");
    return;
  }

  for (const auto& dns : dns_list) {
    net_base::DNSClient::Options opts = {
        .number_of_tries = kDNSNumberOfQueries,
        .per_query_initial_timeout = kDNSTimeoutOfQueries,
        .interface = iface_name_,
        .name_server = dns,
    };
    int diagnostic_id = AssignDiagnosticId(
        Type::kResolveTargetServerIP,
        "Resolving " + url.host() + " with DNS " + dns.ToString());
    dns_queries_[dns] = dns_client_factory_->Resolve(
        ip_family_, url.host(),
        base::BindOnce(&ConnectionDiagnostics::OnHostResolutionComplete,
                       weak_ptr_factory_.GetWeakPtr(), diagnostic_id, dns),
        opts);
  }
}

void ConnectionDiagnostics::StartDNSServerPingDiagnostic() {
  dns_ping_running_ = true;
  int dns_diag_id =
      AssignDiagnosticId(Type::kPingDNSServers, "Ping DNS servers");
  dispatcher_->PostTask(
      FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingDNSServers,
                                weak_ptr_factory_.GetWeakPtr(), dns_diag_id));
}

void ConnectionDiagnostics::PingDNSServers(int dns_diag_id) {
  if (dns_list_.empty()) {
    dns_ping_running_ = false;
    LogEvent(AssignDiagnosticId(Type::kPingDNSServers, "Pinging DNS servers"),
             Type::kPingDNSServers, Result::kSuccess,
             "Skipped because DNS servers are not defined");
    return;
  }

  for (size_t i = 0; i < dns_list_.size(); ++i) {
    const auto& dns_server_ip_addr = dns_list_[i];
    int diag_id = AssignDiagnosticId(
        Type::kPingDNSServers, "Pinging " + dns_server_ip_addr.ToString());
    auto icmp_session = icmp_session_factory_->SendPingRequest(
        dns_server_ip_addr, iface_index_, iface_name_, logging_tag_,
        base::BindOnce(&ConnectionDiagnostics::OnPingDNSServerComplete,
                       weak_ptr_factory_.GetWeakPtr(), diag_id, i),
        dispatcher_);
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
    dns_ping_running_ = false;
    LogEvent(dns_diag_id, Type::kPingDNSServers, Result::kFailure,
             "Could not start ping for any of the given DNS servers");
  } else {
    ClearDiagnosticId(dns_diag_id);
  }
}

void ConnectionDiagnostics::OnPingDNSServerComplete(
    int diagnostic_id,
    int dns_server_index,
    const std::vector<base::TimeDelta>& result) {
  bool found = id_to_pending_dns_server_icmp_session_.erase(dns_server_index);
  dns_ping_running_ = !id_to_pending_dns_server_icmp_session_.empty();
  if (!found) {
    LogEvent(diagnostic_id, Type::kPingDNSServers, Result::kFailure,
             "No matching pending DNS server ICMP session found");
    return;
  }
  OnPingResult(diagnostic_id, Type::kPingDNSServers,
               dns_list_[dns_server_index], result);
}

void ConnectionDiagnostics::OnHostResolutionComplete(
    int diagnostic_id,
    const net_base::IPAddress& dns,
    const net_base::DNSClient::Result& result) {
  dns_queries_.erase(dns);
  if (result.has_value()) {
    std::string message = dns.ToString() + " returned ";
    std::string sep = "";
    for (const auto addr : *result) {
      message += sep;
      message += addr.ToString();
      sep = ", ";
    }
    LogEvent(diagnostic_id, Type::kResolveTargetServerIP, Result::kSuccess,
             message);
    target_url_addresses_.insert(result->begin(), result->end());
  } else {
    std::string message =
        base::StrCat({"DNS ", dns.ToString(), ": ",
                      net_base::DNSClient::ErrorName(result.error())});
    LogEvent(diagnostic_id, Type::kResolveTargetServerIP, Result::kFailure,
             message);
  }

  // Wait for all DNS queries to complete first.
  if (!dns_queries_.empty()) {
    return;
  }

  host_resolution_running_ = false;

  if (target_url_addresses_.empty()) {
    int diagnostic_id =
        AssignDiagnosticId(Type::kPingTargetServer, "Pinging host server");
    LogEvent(diagnostic_id, Type::kPingTargetServer, Result::kFailure,
             "Failed to initiate ping to host server: no DNS result");
    return;
  }

  host_ping_running_ = true;

  // Otherwise starts pinging the addresses of the target host.
  for (const auto& addr : target_url_addresses_) {
    int diagnostic_id = AssignDiagnosticId(Type::kPingTargetServer,
                                           "Pinging " + addr.ToString());
    auto icmp_session = icmp_session_factory_->SendPingRequest(
        addr, iface_index_, iface_name_, logging_tag_,
        base::BindOnce(&ConnectionDiagnostics::OnPingHostComplete,
                       weak_ptr_factory_.GetWeakPtr(), diagnostic_id, addr),
        dispatcher_);
    if (!icmp_session) {
      LogEvent(diagnostic_id, Type::kPingTargetServer, Result::kFailure,
               "Failed to initiate ping to " + addr.ToString());
      continue;
    }
    host_icmp_sessions_[addr] = std::move(icmp_session);
  }

  if (host_icmp_sessions_.empty()) {
    host_ping_running_ = false;
    // Explicitly check if ConnectionDiagnostics should stop since there is
    // no log event associated with this case.
    if (!IsRunning()) {
      Stop();
    }
  }
}

void ConnectionDiagnostics::OnPingHostComplete(
    int diagnostic_id,
    const net_base::IPAddress& address_pinged,
    const std::vector<base::TimeDelta>& result) {
  host_icmp_sessions_.erase(address_pinged);
  host_ping_running_ = !host_icmp_sessions_.empty();
  OnPingResult(diagnostic_id, Type::kPingTargetServer, address_pinged, result);
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
    std::optional<net_base::IPAddress> gateway,
    const std::vector<net_base::IPAddress>& dns_list,
    std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
    std::unique_ptr<IcmpSessionFactory> icmp_session_factory,
    std::string_view logging_tag,
    EventDispatcher* dispatcher) {
  return std::make_unique<ConnectionDiagnostics>(
      iface_name, iface_index, ip_family, gateway, dns_list,
      std::move(dns_client_factory), std::move(icmp_session_factory),
      logging_tag, dispatcher);
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

void ConnectionDiagnostics::StartGatewayPingDiagnostic() {
  int diagnostic_id = AssignDiagnosticId(Type::kPingGateway, "Pinging gateway");
  gateway_ping_running_ = true;
  dispatcher_->PostTask(
      FROM_HERE, base::BindOnce(&ConnectionDiagnostics::PingGateway,
                                weak_ptr_factory_.GetWeakPtr(), diagnostic_id));
}

void ConnectionDiagnostics::PingGateway(int diagnostic_id) {
  if (!gateway_) {
    gateway_ping_running_ = false;
    LogEvent(diagnostic_id, Type::kPingGateway, Result::kSuccess,
             "Skipped because gateway is not defined");
    return;
  }
  gateway_icmp_session_ = icmp_session_factory_->SendPingRequest(
      *gateway_, iface_index_, iface_name_, logging_tag_,
      base::BindOnce(&ConnectionDiagnostics::OnPingGatewayComplete,
                     weak_ptr_factory_.GetWeakPtr(), diagnostic_id),
      dispatcher_);
  if (!gateway_icmp_session_) {
    gateway_ping_running_ = false;
    LogEvent(diagnostic_id, Type::kPingGateway, Result::kFailure,
             "Failed to initiate ping to " + gateway_->ToString());
  }
}

void ConnectionDiagnostics::OnPingGatewayComplete(
    int diagnostic_id, const std::vector<base::TimeDelta>& result) {
  gateway_ping_running_ = false;
  OnPingResult(diagnostic_id, Type::kPingGateway, *gateway_, result);
}

}  // namespace shill
