// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ppp_daemon.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <net-base/ipv4_address.h>
#include <net-base/network_config.h>

extern "C" {
// A struct member in pppd.h has the name 'class'.
#define class class_num
// pppd.h defines a bool type.
#define bool pppd_bool_t
#include <pppd/pppd.h>
#undef bool
#undef class
}

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_number_conversions.h>
#include <net-base/ip_address.h>
#include <net-base/process_manager.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/external_task.h"
#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kPPP;
}  // namespace Logging

namespace {

const char kDaemonPath[] = "/usr/sbin/pppd";
const uint32_t kUnspecifiedValue = UINT32_MAX;

}  // namespace

PPPDaemon::Options::Options()
    : debug(false),
      no_detach(false),
      no_default_route(false),
      use_peer_dns(false),
      use_shim_plugin(true),
      lcp_echo_interval(kUnspecifiedValue),
      lcp_echo_failure(kUnspecifiedValue),
      max_fail(kUnspecifiedValue),
      use_ipv6(false) {}

const char PPPDaemon::kShimPluginPath[] = SHIMDIR "/shill-pppd-plugin.so";

std::unique_ptr<ExternalTask> PPPDaemon::Start(
    ControlInterface* control_interface,
    net_base::ProcessManager* process_manager,
    const base::WeakPtr<RpcTaskDelegate>& task_delegate,
    const PPPDaemon::Options& options,
    const std::string& device,
    PPPDaemon::DeathCallback death_callback,
    Error* error) {
  std::vector<std::string> arguments;

  // pppd runs under the non-root 'shill' group, so we need to explicitly tell
  // pppd to allow certain privileged options.
  arguments.push_back("privgroup");
  arguments.push_back("shill");

  if (options.debug) {
    arguments.push_back("debug");
  }
  if (options.no_detach) {
    arguments.push_back("nodetach");
  }
  if (options.no_default_route) {
    arguments.push_back("nodefaultroute");
  }
  if (options.use_peer_dns) {
    arguments.push_back("usepeerdns");
  }
  if (options.use_shim_plugin) {
    arguments.push_back("plugin");
    arguments.push_back(kShimPluginPath);
  }
  if (options.lcp_echo_interval != kUnspecifiedValue) {
    arguments.push_back("lcp-echo-interval");
    arguments.push_back(base::NumberToString(options.lcp_echo_interval));
  }
  if (options.lcp_echo_failure != kUnspecifiedValue) {
    arguments.push_back("lcp-echo-failure");
    arguments.push_back(base::NumberToString(options.lcp_echo_failure));
  }
  if (options.max_fail != kUnspecifiedValue) {
    arguments.push_back("maxfail");
    arguments.push_back(base::NumberToString(options.max_fail));
  }
  if (options.use_ipv6) {
    arguments.push_back("+ipv6");
    arguments.push_back("ipv6cp-use-ipaddr");
  }

  arguments.push_back(device);

  auto task =
      std::make_unique<ExternalTask>(control_interface, process_manager,
                                     task_delegate, std::move(death_callback));

  std::map<std::string, std::string> environment;
  if (task->Start(base::FilePath(kDaemonPath), arguments, environment, true,
                  error)) {
    return task;
  }
  return nullptr;
}

// static
std::string PPPDaemon::GetInterfaceName(
    const std::map<std::string, std::string>& configuration) {
  if (base::Contains(configuration, kPPPInterfaceName)) {
    return configuration.find(kPPPInterfaceName)->second;
  }
  return std::string();
}

// static
net_base::NetworkConfig PPPDaemon::ParseNetworkConfig(
    const std::map<std::string, std::string>& configuration) {
  net_base::NetworkConfig config;
  std::optional<net_base::IPv4Address> external_address;
  for (const auto& [key, value] : configuration) {
    SLOG(2) << "Processing: " << key << " -> " << value;
    if (key == kPPPInternalIP4Address) {
      config.ipv4_address = net_base::IPv4CIDR::CreateFromStringAndPrefix(
          value, net_base::IPv4CIDR::kMaxPrefixLength);
      if (!config.ipv4_address.has_value()) {
        LOG(ERROR) << "Failed to parse internal IPv4 address: " << value;
      }
    } else if (key == kPPPExternalIP4Address) {
      external_address = net_base::IPv4Address::CreateFromString(value);
      if (!external_address.has_value()) {
        LOG(WARNING) << "Failed to parse external IPv4 address: " << value;
      }
    } else if (key == kPPPGatewayAddress) {
      config.ipv4_gateway = net_base::IPv4Address::CreateFromString(value);
      if (!config.ipv4_gateway.has_value()) {
        LOG(WARNING) << "Failed to parse internal gateway address: " << value;
      }
    } else if (key == kPPPDNS1) {
      const std::optional<net_base::IPAddress> dns_server =
          net_base::IPAddress::CreateFromString(value);
      if (!dns_server.has_value()) {
        LOG(WARNING) << "Failed to parse DNS1: " << value;
        continue;
      }
      config.dns_servers.insert(config.dns_servers.begin(), *dns_server);
    } else if (key == kPPPDNS2) {
      const std::optional<net_base::IPAddress> dns_server =
          net_base::IPAddress::CreateFromString(value);
      if (!dns_server.has_value()) {
        LOG(WARNING) << "Failed to parse DNS2: " << value;
        continue;
      }
      config.dns_servers.push_back(*dns_server);
    } else if (key == kPPPLNSAddress) {
      // This is really a L2TPIPsec property. But it's sent to us by
      // our PPP plugin.
      const std::optional<net_base::IPCIDR> prefix =
          net_base::IPCIDR::CreateFromStringAndPrefix(
              value, net_base::IPv4CIDR::kMaxPrefixLength);
      if (!prefix.has_value()) {
        LOG(WARNING) << "Failed to parse LNS address: " << value;
        continue;
      }
      config.excluded_route_prefixes.push_back(*prefix);
    } else if (key == kPPPMRU) {
      int mru;
      if (!base::StringToInt(value, &mru)) {
        LOG(WARNING) << "Failed to parse MRU: " << value;
        continue;
      }
      if (mru < net_base::NetworkConfig::kMinIPv4MTU) {
        LOG(INFO) << __func__ << " MRU " << mru
                  << " is too small; adjusting up to "
                  << net_base::NetworkConfig::kMinIPv4MTU;
        mru = net_base::NetworkConfig::kMinIPv4MTU;
      }
      config.mtu = mru;
    } else {
      SLOG(2) << "Key ignored.";
    }
  }

  // The presence of the external address suggests that this is a p2p network.
  // No gateway is needed.
  if (external_address.has_value()) {
    config.ipv4_gateway = std::nullopt;
  }
  return config;
}

// static
Service::ConnectFailure PPPDaemon::ExitStatusToFailure(int exit) {
  switch (exit) {
    case EXIT_OK:
      return Service::kFailureNone;
    case EXIT_PEER_AUTH_FAILED:
    case EXIT_AUTH_TOPEER_FAILED:
      return Service::kFailurePPPAuth;
    default:
      return Service::kFailureUnknown;
  }
}

// static
Service::ConnectFailure PPPDaemon::ParseExitFailure(
    const std::map<std::string, std::string>& dict) {
  const auto it = dict.find(kPPPExitStatus);
  if (it == dict.end()) {
    LOG(ERROR) << "Failed to find the failure status in the dict";
    return Service::kFailureInternal;
  }
  int exit = 0;
  if (!base::StringToInt(it->second, &exit)) {
    LOG(ERROR) << "Failed to parse the failure status from the dict, value: "
               << it->second;
    return Service::kFailureInternal;
  }
  return ExitStatusToFailure(exit);
}

}  // namespace shill
