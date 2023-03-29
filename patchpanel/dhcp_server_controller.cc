// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <linux/capability.h>

#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "patchpanel/system.h"

namespace patchpanel {
namespace {
constexpr char kDnsmasqPath[] = "/usr/sbin/dnsmasq";
constexpr char kLeaseTime[] = "12h";  // 12 hours
}  // namespace

using Config = DHCPServerController::Config;

// static
std::optional<Config> Config::Create(const shill::IPAddress& host_ip,
                                     const shill::IPAddress& start_ip,
                                     const shill::IPAddress& end_ip) {
  // All the fields should be valid IPv4 IP.
  constexpr auto kValidFamily = shill::IPAddress::kFamilyIPv4;
  if (!(host_ip.IsValid() && host_ip.family() == kValidFamily &&
        start_ip.IsValid() && start_ip.family() == kValidFamily &&
        end_ip.IsValid() && end_ip.family() == kValidFamily)) {
    return std::nullopt;
  }

  // The start_ip and end_ip should be in the same subnet as host_ip.
  if (!(host_ip.CanReachAddress(start_ip) && host_ip.CanReachAddress(end_ip))) {
    return std::nullopt;
  }

  // end_ip should not be smaller than or start_ip.
  if (end_ip < start_ip) {
    return std::nullopt;
  }

  const auto netmask = shill::IPAddress::GetAddressMaskFromPrefix(
      kValidFamily, host_ip.prefix());
  return Config(host_ip.ToString(), netmask.ToString(), start_ip.ToString(),
                end_ip.ToString());
}

Config::Config(const std::string& host_ip,
               const std::string& netmask,
               const std::string& start_ip,
               const std::string& end_ip)
    : host_ip_(host_ip),
      netmask_(netmask),
      start_ip_(start_ip),
      end_ip_(end_ip) {}

std::ostream& operator<<(std::ostream& os, const Config& config) {
  os << "{host_ip: " << config.host_ip() << ", netmask: " << config.netmask()
     << ", start_ip: " << config.start_ip() << ", end_ip: " << config.end_ip()
     << "}";
  return os;
}

DHCPServerController::DHCPServerController(const std::string& ifname)
    : ifname_(ifname), process_manager_(shill::ProcessManager::GetInstance()) {}

DHCPServerController::~DHCPServerController() {
  Stop();
}

bool DHCPServerController::Start(const Config& config,
                                 ExitCallback exit_callback) {
  if (IsRunning()) {
    LOG(ERROR) << "DHCP server is still running: " << ifname_
               << ", old config=" << *config_;
    return false;
  }

  LOG(INFO) << "Starting DHCP server at: " << ifname_ << ", config: " << config;
  const std::vector<std::string> dnsmasq_args = {
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

  shill::ProcessManager::MinijailOptions minijail_options = {};
  minijail_options.user = kPatchpaneldUser;
  minijail_options.group = kPatchpaneldGroup;
  minijail_options.capmask = CAP_TO_MASK(CAP_NET_ADMIN) |
                             CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
                             CAP_TO_MASK(CAP_NET_RAW);

  const pid_t pid = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kDnsmasqPath), dnsmasq_args, /*environment=*/{},
      minijail_options,
      base::BindOnce(&DHCPServerController::OnProcessExitedUnexpectedly,
                     weak_ptr_factory_.GetWeakPtr()));
  if (pid < 0) {
    LOG(ERROR) << "Failed to start the DHCP server: " << ifname_;
    return false;
  }

  pid_ = pid;
  config_ = config;
  exit_callback_ = std::move(exit_callback);
  return true;
}

void DHCPServerController::Stop() {
  if (!IsRunning()) {
    return;
  }

  LOG(INFO) << "Stopping DHCP server at: " << ifname_;
  process_manager_->StopProcess(*pid_);

  pid_ = std::nullopt;
  config_ = std::nullopt;
  exit_callback_.Reset();
}

bool DHCPServerController::IsRunning() const {
  return pid_.has_value();
}

void DHCPServerController::OnProcessExitedUnexpectedly(int exit_status) {
  LOG(ERROR) << "dnsmasq exited unexpectedly, status: " << exit_status;

  pid_ = std::nullopt;
  config_ = std::nullopt;
  std::move(exit_callback_).Run(exit_status);
}

}  // namespace patchpanel
