// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <fcntl.h>
#include <linux/capability.h>

#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>

#include "patchpanel/metrics.h"
#include "patchpanel/system.h"

namespace patchpanel {
namespace {
constexpr char kDnsmasqPath[] = "/usr/sbin/dnsmasq";
constexpr char kLeaseTime[] = "12h";  // 12 hours

constexpr char kDHCPRequest[] = "DHCPREQUEST";
constexpr char kDHCPAck[] = "DHCPACK";
constexpr char kDHCPNak[] = "DHCPNAK";
constexpr char kDHCPDecline[] = "DHCPDECLINE";
constexpr std::pair<const char*, DHCPServerUmaEvent> kEventTable[] = {
    {kDHCPRequest, DHCPServerUmaEvent::kDHCPMessageRequest},
    {kDHCPAck, DHCPServerUmaEvent::kDHCPMessageAck},
    {kDHCPNak, DHCPServerUmaEvent::kDHCPMessageNak},
    {kDHCPDecline, DHCPServerUmaEvent::kDHCPMessageDecline},
};

}  // namespace

using Config = DHCPServerController::Config;

// static
std::optional<Config> Config::Create(
    const net_base::IPv4CIDR& host_cidr,
    const net_base::IPv4Address& start_ip,
    const net_base::IPv4Address& end_ip,
    const std::vector<net_base::IPv4Address>& dns_servers,
    const std::vector<std::string>& domain_searches,
    const std::optional<int>& mtu,
    const DHCPOptions& dhcp_options) {
  // The start_ip and end_ip should be in the same subnet as host_cidr.
  if (!(host_cidr.InSameSubnetWith(start_ip) &&
        host_cidr.InSameSubnetWith(end_ip))) {
    return std::nullopt;
  }

  // end_ip should not be smaller than or start_ip.
  if (end_ip < start_ip) {
    return std::nullopt;
  }

  // Transform std::vector<IPv4Address> to std::vector<std::string>.
  std::vector<std::string> dns_server_strs;
  for (const auto& ip : dns_servers) {
    dns_server_strs.push_back(ip.ToString());
  }

  const std::string mtu_str = (mtu) ? std::to_string(*mtu) : "";

  return Config(host_cidr.address().ToString(),
                host_cidr.ToNetmask().ToString(), start_ip.ToString(),
                end_ip.ToString(), base::JoinString(dns_server_strs, ","),
                base::JoinString(domain_searches, ","), mtu_str, dhcp_options);
}

Config::Config(const std::string& host_ip,
               const std::string& netmask,
               const std::string& start_ip,
               const std::string& end_ip,
               const std::string& dns_servers,
               const std::string& domain_searches,
               const std::string& mtu,
               const DHCPOptions& dhcp_options)
    : host_ip_(host_ip),
      netmask_(netmask),
      start_ip_(start_ip),
      end_ip_(end_ip),
      dns_servers_(dns_servers),
      domain_searches_(domain_searches),
      mtu_(mtu),
      dhcp_options_(dhcp_options) {}

std::ostream& operator<<(std::ostream& os, const Config& config) {
  os << "{host_ip: " << config.host_ip() << ", netmask: " << config.netmask()
     << ", start_ip: " << config.start_ip() << ", end_ip: " << config.end_ip()
     << "}";
  return os;
}

DHCPServerController::DHCPServerController(
    MetricsLibraryInterface* metrics,
    const std::string& dhcp_events_metric_name,
    const std::string& ifname)
    : metrics_(metrics),
      dhcp_events_metric_name_(dhcp_events_metric_name),
      ifname_(ifname),
      process_manager_(shill::ProcessManager::GetInstance()) {
  DCHECK(metrics_);
}

DHCPServerController::~DHCPServerController() {
  Stop();
}

bool DHCPServerController::Start(const Config& config,
                                 ExitCallback exit_callback) {
  metrics_->SendEnumToUMA(dhcp_events_metric_name_, DHCPServerUmaEvent::kStart);
  if (IsRunning()) {
    LOG(ERROR) << "DHCP server is still running: " << ifname_
               << ", old config=" << *config_;
    return false;
  }

  LOG(INFO) << "Starting DHCP server at: " << ifname_ << ", config: " << config;
  std::vector<std::string> dnsmasq_args = {
      "--log-facility=-",      // Logs to stderr.
      "--dhcp-authoritative",  // dnsmasq is the only DHCP server on a network.
      "--keep-in-foreground",  // Use foreground mode to prevent forking.
      "--log-dhcp",            // Log the DHCP event.
      "--no-ping",             // (b/257377981): Speed up the negotiation.
      "--port=0",              // Disable DNS.
      "--leasefile-ro",        // Do not use leasefile.
      base::StringPrintf("--interface=%s", ifname_.c_str()),
      base::StringPrintf("--dhcp-range=%s,%s,%s,%s", config.start_ip().c_str(),
                         config.end_ip().c_str(), config.netmask().c_str(),
                         kLeaseTime),
      base::StringPrintf("--dhcp-option=option:netmask,%s",
                         config.netmask().c_str()),
      base::StringPrintf("--dhcp-option=option:router,%s",
                         config.host_ip().c_str()),
  };
  if (!config.dns_servers().empty()) {
    dnsmasq_args.push_back(base::StringPrintf(
        "--dhcp-option=option:dns-server,%s", config.dns_servers().c_str()));
  }
  if (!config.domain_searches().empty()) {
    dnsmasq_args.push_back(
        base::StringPrintf("--dhcp-option=option:domain-search,%s",
                           config.domain_searches().c_str()));
  }
  if (!config.mtu().empty()) {
    dnsmasq_args.push_back(base::StringPrintf(
        "--dhcp-option-force=option:mtu,%s", config.mtu().c_str()));
  }
  for (const auto& [tag, content] : config.dhcp_options()) {
    dnsmasq_args.push_back(
        base::StringPrintf("--dhcp-option-force=%u,%s", tag, content.c_str()));
  }

  shill::ProcessManager::MinijailOptions minijail_options = {};
  minijail_options.user = kPatchpaneldUser;
  minijail_options.group = kPatchpaneldGroup;
  minijail_options.capmask = CAP_TO_MASK(CAP_NET_ADMIN) |
                             CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
                             CAP_TO_MASK(CAP_NET_RAW);

  int stderr_fd = -1;
  const pid_t pid = process_manager_->StartProcessInMinijailWithPipes(
      FROM_HERE, base::FilePath(kDnsmasqPath), dnsmasq_args, /*environment=*/{},
      minijail_options,
      base::BindOnce(&DHCPServerController::OnProcessExitedUnexpectedly,
                     weak_ptr_factory_.GetWeakPtr()),
      {nullptr, nullptr, &stderr_fd});
  if (pid < 0) {
    LOG(ERROR) << "Failed to start the DHCP server: " << ifname_;
    return false;
  }
  log_fd_.reset(stderr_fd);

  // Set stderr_fd non-blocking.
  const int opt = fcntl(stderr_fd, F_GETFL) | O_NONBLOCK;
  if (fcntl(stderr_fd, F_SETFL, opt) < 0) {
    LOG(ERROR) << "Failed to set the stderr fd to non-blocking";
    return false;
  }

  log_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      stderr_fd,
      base::BindRepeating(&DHCPServerController::OnDnsmasqLogReady,
                          // The callback will not outlive the object.
                          base::Unretained(this)));

  pid_ = pid;
  config_ = config;
  exit_callback_ = std::move(exit_callback);
  metrics_->SendEnumToUMA(dhcp_events_metric_name_,
                          DHCPServerUmaEvent::kStartSuccess);
  return true;
}

void DHCPServerController::Stop() {
  if (!IsRunning()) {
    return;
  }

  metrics_->SendEnumToUMA(dhcp_events_metric_name_, DHCPServerUmaEvent::kStop);
  LOG(INFO) << "Stopping DHCP server at: " << ifname_;
  if (process_manager_->StopProcess(*pid_)) {
    metrics_->SendEnumToUMA(dhcp_events_metric_name_,
                            DHCPServerUmaEvent::kStopSuccess);
  } else {
    LOG(WARNING) << "The DHCP server process cannot be terminated";
  }

  pid_ = std::nullopt;
  config_ = std::nullopt;
  exit_callback_.Reset();

  log_watcher_.reset();
  log_fd_.reset();
  mac_addr_to_hostname_.clear();
}

bool DHCPServerController::IsRunning() const {
  return pid_.has_value();
}

std::string DHCPServerController::GetClientHostname(
    const std::string& mac_addr) const {
  const auto it = mac_addr_to_hostname_.find(mac_addr);
  if (it != mac_addr_to_hostname_.end()) {
    return it->second;
  }
  return "";
}

void DHCPServerController::OnProcessExitedUnexpectedly(int exit_status) {
  LOG(ERROR) << "dnsmasq exited unexpectedly, status: " << exit_status;

  pid_ = std::nullopt;
  config_ = std::nullopt;
  std::move(exit_callback_).Run(exit_status);
}

void DHCPServerController::OnDnsmasqLogReady() {
  static std::string stash_token;
  static char buf[256];

  while (true) {
    const ssize_t len = read(log_fd_.get(), buf, sizeof(buf));
    if (len <= 0) {
      break;
    }

    // Split to string.
    base::CStringTokenizer tokenizer(buf, buf + len, "\n");
    tokenizer.set_options(base::StringTokenizer::RETURN_DELIMS);
    while (tokenizer.GetNext()) {
      if (tokenizer.token_is_delim()) {
        HandleDnsmasqLog(stash_token);
        stash_token = "";
      } else {
        stash_token += tokenizer.token();
      }
    }
  }
}

void DHCPServerController::HandleDnsmasqLog(std::string_view log) {
  for (const auto& [msg, event] : kEventTable) {
    if (log.find(msg) != std::string::npos) {
      metrics_->SendEnumToUMA(dhcp_events_metric_name_, event);
      break;
    }
  }

  // The client hostname is considered PII. We should not print it in syslog.
  // Before parsing the hostname by "DHCPACK" log, the hostname only appears
  // with "client provides name: <hostname>". So the steps are:
  // 1. Skip the log with "client provides name"
  // 2. Get the hostname from the log: "DHCPACK"
  // 3. Replace all the known hostnames to "<redacted>"
  if (log.find("client provides name") != std::string::npos) {
    return;
  }

  const size_t pos = log.find("DHCPACK");
  if (pos != std::string::npos) {
    // The log format: DHCPACK(<iface>) <IP> <MAC address> [hostname]
    const std::vector<std::string_view> tokens = base::SplitStringPiece(
        log.substr(pos), base::kWhitespaceASCII, base::TRIM_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);
    if (tokens.size() >= 4) {
      // Handle the hostname with whitespace characters.
      const std::string mac_addr(tokens[2]);
      const std::string hostname(tokens[3]);
      mac_addr_to_hostname_[mac_addr] = hostname;
      hostname_set.insert(hostname);
    }
  }

  if (hostname_set.empty()) {
    LOG(INFO) << log;
  } else {
    std::vector<std::string> tokens =
        base::SplitString(log, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    for (auto& token : tokens) {
      if (base::Contains(hostname_set, token)) {
        token = "<redacted>";
      }
    }
    LOG(INFO) << base::JoinString(tokens, " ");
  }
}

}  // namespace patchpanel
