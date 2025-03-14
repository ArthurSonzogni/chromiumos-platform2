// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpcd_proxy.h"

#include <signal.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/process/process_iterator.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/files/file_util.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/process_manager.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/store/key_value_store.h"
#include "shill/technology.h"

namespace shill {
namespace {

constexpr char kDHCPCDExecutableName[] = "dhcpcd";
constexpr char kDHCPCDPath[] = "/sbin/dhcpcd";
constexpr char kDHCPCDUser[] = "dhcp";
constexpr char kDHCPCDGroup[] = "dhcp";
// %s - interface name.
constexpr char kDHCPCDPathFormatLease[] = "var/lib/dhcpcd/%s.lease";
// %s - interface name; %d - IP family.
constexpr char kDHCPCDPathFormatPID[] = "var/run/dhcpcd/dhcpcd-%s-%d.pid";

// Returns a list of dhcpcd args. Redacts the hostname and the lease name for
// logging if |redact_args| is set to true.
std::vector<std::string> GetDhcpcdArgs(Technology technology,
                                       const DHCPClientProxy::Options& options,
                                       std::string_view interface,
                                       net_base::IPFamily family,
                                       bool redact_args) {
  std::vector<std::string> args = {
      // Run in foreground.
      "-B",
      // Static value for Vendor class info.
      "-i",
      "chromeos",
      // Only warnings+errors to stderr.
      "-q",
      // Request the captive portal URI.
      "-o",
      "captive_portal_uri",
      // Request the Web Proxy Auto-Discovery.
      "-o",
      "wpad_url",
      // Send a default clientid of the hardware family and the hardware
      // address.
      "--clientid",
      // No initial randomised delay.
      "--nodelay",
      // Do not configure the system.
      "--noconfigure",
  };

  if (family == net_base::IPFamily::kIPv4) {
    args.insert(args.end(),
                {
                    // IPv4 only.
                    "-4",
                    // Don't request or claim the address by ARP.
                    "-A",
                    // Don't receive link messages for carrier status.
                    "-K",
                });
  } else {
    // Note that ChromeOS only support IA_PD for DHCPv6.
    args.insert(args.end(), {
                                "-6",
                                "--noipv6rs",
                                "--ia_pd",
                            });
  }

  // Request hostname from server.
  if (!options.hostname.empty()) {
    args.insert(args.end(),
                {"-h", redact_args ? "<redacted_hostname>" : options.hostname});
  }

  if (options.use_rfc_8925 && family == net_base::IPFamily::kIPv4) {
    // Request option 108 to prefer IPv6-only. If server also supports this, no
    // dhcp lease will be assigned and dhcpcd will notify shill with an
    // IPv6OnlyPreferred StatusChanged event.
    args.insert(args.end(), {"-o", "ipv6_only_preferred"});
  }

  // TODO(jiejiang): This will also include the WiFi Direct GC mode now. We may
  // want to check if we should enable it in the future.
  if (options.apply_dscp && technology == Technology::kWiFi) {
    // This flag is added by https://crrev.com/c/4861699.
    args.push_back("--apply_dscp");
  }

  args.push_back(std::string(interface));

  return args;
}

// Runs the dhcpcd process in the minijail.
pid_t RunDHCPCDInMinijail(net_base::ProcessManager* process_manager,
                          const std::vector<std::string>& args,
                          bool need_cap) {
  net_base::ProcessManager::MinijailOptions minijail_options;
  minijail_options.user = kDHCPCDUser;
  minijail_options.group = kDHCPCDGroup;
  if (need_cap) {
    minijail_options.capmask =
        CAP_TO_MASK(CAP_NET_BIND_SERVICE) | CAP_TO_MASK(CAP_NET_BROADCAST) |
        CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  }
  minijail_options.inherit_supplementary_groups = false;

  return process_manager->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kDHCPCDPath), args, {}, minijail_options,
      base::DoNothing());
}

std::optional<DHCPClientProxy::EventReason> GetEventReason(
    const std::map<std::string, std::string>& configuration,
    std::string_view logging_tag) {
  // Constants used as event type got from dhcpcd.
  static constexpr auto kEventReasonTable =
      base::MakeFixedFlatMap<std::string_view, DHCPClientProxy::EventReason>({
          {"BOUND", DHCPClientProxy::EventReason::kBound},
          {"FAIL", DHCPClientProxy::EventReason::kFail},
          {"GATEWAY-ARP", DHCPClientProxy::EventReason::kGatewayArp},
          {"NAK", DHCPClientProxy::EventReason::kNak},
          {"REBIND", DHCPClientProxy::EventReason::kRebind},
          {"REBOOT", DHCPClientProxy::EventReason::kReboot},
          {"RENEW", DHCPClientProxy::EventReason::kRenew},
          {"IPV6-ONLY-PREFERRED",
           DHCPClientProxy::EventReason::kIPv6OnlyPreferred},
          {"BOUND6", DHCPClientProxy::EventReason::kBound6},
          {"REBIND6", DHCPClientProxy::EventReason::kRebind6},
          {"REBOOT6", DHCPClientProxy::EventReason::kReboot6},
          {"RENEW6", DHCPClientProxy::EventReason::kRenew6},
      });

  const auto conf_iter =
      configuration.find(DHCPv4Config::kConfigurationKeyReason);
  if (conf_iter == configuration.end()) {
    LOG(WARNING) << logging_tag << " " << __func__ << ": "
                 << DHCPv4Config::kConfigurationKeyReason
                 << " is missing from configuration";
    return std::nullopt;
  }

  const auto table_iter = kEventReasonTable.find(conf_iter->second);
  if (table_iter == kEventReasonTable.end()) {
    LOG(INFO) << logging_tag << " " << __func__
              << ": Ignore the reason: " << conf_iter->second;
    return std::nullopt;
  }
  return table_iter->second;
}

}  // namespace

DHCPCDProxy::DHCPCDProxy(net_base::ProcessManager* process_manager,
                         std::string_view interface,
                         DHCPClientProxy::EventHandler* handler,
                         base::ScopedClosureRunner destroy_cb,
                         std::string_view logging_tag)
    : DHCPClientProxy(interface, handler),
      process_manager_(process_manager),
      destroy_cb_(std::move(destroy_cb)),
      logging_tag_(logging_tag) {}

DHCPCDProxy::~DHCPCDProxy() = default;

bool DHCPCDProxy::IsReady() const {
  return true;
}

bool DHCPCDProxy::Rebind() {
  return RunDHCPCDWithArgs(
      std::vector<std::string>{"-4", "--noconfigure", "--rebind", interface_});
}

bool DHCPCDProxy::Release() {
  return RunDHCPCDWithArgs(
      std::vector<std::string>{"-4", "--noconfigure", "--release", interface_});
}

bool DHCPCDProxy::RunDHCPCDWithArgs(const std::vector<std::string>& args) {
  const pid_t pid =
      RunDHCPCDInMinijail(process_manager_, args, /*need_cap=*/false);
  if (pid == net_base::ProcessManager::kInvalidPID) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Failed to run dhcpcd with args:"
               << base::JoinString(args, " ");
    return false;
  }

  return true;
}

void DHCPCDProxy::OnDHCPEvent(
    const std::map<std::string, std::string>& configuration) {
  const auto iter =
      configuration.find(DHCPv4Config::kConfigurationKeyInterface);
  if (iter == configuration.end() || iter->second != interface_) {
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Interface is mismatched";
    return;
  }

  const std::optional<EventReason> reason =
      GetEventReason(configuration, logging_tag_);
  if (!reason.has_value()) {
    return;
  }

  net_base::NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;

  if (reason == EventReason::kBound6 || reason == EventReason::kRebind6 ||
      reason == EventReason::kReboot6 || reason == EventReason::kRenew6) {
    network_config = ParsePDConfiguration(configuration);
    // In DHCPv6, Each IA_PD can have different T1/T2 and each prefix can have
    // different valid-lifetime and preferred-lifetime. Those are left to be
    // handled by dhcpcd itself and we are not setting dhcp_data.lease_duration
    // in shill.
    handler_->OnDHCPEvent(*reason, network_config, dhcp_data);
    return;
  }

  if (NeedConfiguration(*reason) &&
      !DHCPv4Config::ParseConfiguration(
          ConvertConfigurationToKeyValueStore(configuration), &network_config,
          &dhcp_data, logging_tag_)) {
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Error parsing network configuration from DHCP client. "
                 << "The following configuration might be partial: "
                 << network_config;
  }
  handler_->OnDHCPEvent(*reason, network_config, dhcp_data);
}

net_base::NetworkConfig DHCPCDProxy::ParsePDConfiguration(
    const std::map<std::string, std::string>& configuration) {
  net_base::NetworkConfig result;
  for (const auto& [key, value] : configuration) {
    if (key == DHCPv4Config::kConfigurationKeyMTU) {
      int mtu;
      if (base::StringToInt(value, &mtu)) {
        result.mtu = mtu;
      }
    } else if (key.find(DHCPv4Config::kConfigurationKeyPrefixIAPDPrefix) == 0) {
      const auto prefix = net_base::IPv6CIDR::CreateFromCIDRString(value);
      if (prefix) {
        result.ipv6_delegated_prefixes.push_back(*prefix);
      }
    }
  }
  return result;
}

KeyValueStore DHCPCDProxy::ConvertConfigurationToKeyValueStore(
    const std::map<std::string, std::string>& configuration) {
  KeyValueStore store;
  for (const auto& [key, value] : configuration) {
    if (key == DHCPv4Config::kConfigurationKeyIPAddress ||
        key == DHCPv4Config::kConfigurationKeyBroadcastAddress) {
      const std::optional<net_base::IPv4Address> address =
          net_base::IPv4Address::CreateFromString(value);
      if (address.has_value()) {
        store.Set<uint32_t>(key, address->ToInAddr().s_addr);
      }
    } else if (key == DHCPv4Config::kConfigurationKeyRouters ||
               key == DHCPv4Config::kConfigurationKeyDNS) {
      std::vector<uint32_t> addresses;
      for (const auto& str : base::SplitString(
               value, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
        const std::optional<net_base::IPv4Address> address =
            net_base::IPv4Address::CreateFromString(str);
        if (address.has_value()) {
          addresses.push_back(address->ToInAddr().s_addr);
        }
        store.Set<std::vector<uint32_t>>(key, addresses);
      }
    } else if (key == DHCPv4Config::kConfigurationKeySubnetCIDR) {
      int prefix_length;
      if (base::StringToInt(value, &prefix_length)) {
        store.Set<uint8_t>(key, prefix_length);
      }
    } else if (key == DHCPv4Config::kConfigurationKeyMTU) {
      int mtu;
      if (base::StringToInt(value, &mtu)) {
        store.Set<uint16_t>(key, mtu);
      }
    } else if (key == DHCPv4Config::kConfigurationKeyLeaseTime) {
      int lease_time;
      if (base::StringToInt(value, &lease_time)) {
        store.Set<uint32_t>(key, lease_time);
      }
    } else if (key == DHCPv4Config::kConfigurationKeyDomainName ||
               key == DHCPv4Config::kConfigurationKeyCaptivePortalUri ||
               key == DHCPv4Config::kConfigurationKeyClasslessStaticRoutes ||
               key == DHCPv4Config::kConfigurationKeyWebProxyAutoDiscoveryUrl) {
      store.Set<std::string>(key, value);
    } else if (key == DHCPv4Config::kConfigurationKeyDomainSearch) {
      store.Set<std::vector<std::string>>(
          key, base::SplitString(value, " ", base::TRIM_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY));
    } else if (key ==
               DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions) {
      std::vector<uint8_t> options;
      if (base::HexStringToBytes(value, &options)) {
        store.Set<std::vector<uint8_t>>(key, options);
      }
    }
  }
  return store;
}

base::WeakPtr<DHCPCDProxy> DHCPCDProxy::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

DHCPCDProxyFactory::DHCPCDProxyFactory(
    net_base::ProcessManager* process_manager)
    : process_manager_(process_manager) {
  // Kill the dhcpcd processes accidentally left by previous run.
  base::NamedProcessIterator iter(kDHCPCDExecutableName, nullptr);
  while (const base::ProcessEntry* entry = iter.NextProcessEntry()) {
    process_manager_->StopProcessAndBlock(entry->pid());
  }
}

DHCPCDProxyFactory::~DHCPCDProxyFactory() {
  // Clear all the alive dhcpcd processes.
  alive_proxies_.clear();
  CHECK(pids_need_to_stop_.empty());
}

std::unique_ptr<DHCPClientProxy> DHCPCDProxyFactory::Create(
    std::string_view interface,
    Technology technology,
    const DHCPClientProxy::Options& options,
    DHCPClientProxy::EventHandler* handler,
    std::string_view logging_tag,
    net_base::IPFamily family) {
  const std::vector<std::string> args = GetDhcpcdArgs(
      technology, options, interface, family, /*redact_args=*/false);

  const pid_t pid =
      RunDHCPCDInMinijail(process_manager_, args, /*need_cap=*/true);
  if (pid == net_base::ProcessManager::kInvalidPID) {
    LOG(ERROR) << logging_tag << " " << __func__
               << ": Failed to start the dhcpcd process";
    return nullptr;
  }
  pids_need_to_stop_.insert(pid);
  base::ScopedClosureRunner clean_up_closure(base::BindOnce(
      // base::Unretained(this) is safe because the closure won't be passed
      // outside this instance.
      &DHCPCDProxyFactory::CleanUpDhcpcd, base::Unretained(this),
      std::string(interface), family, options, pid, std::string(logging_tag)));

  // Log dhcpcd args but redact the args to exclude PII.
  LOG(INFO) << logging_tag << " " << __func__ << ": Created dhcpcd with pid "
            << pid << " and args: "
            << base::JoinString(
                   GetDhcpcdArgs(technology, options, interface, family,
                                 /*redact_args=*/true),
                   " ");

  // Inject the exit callback with pid information.
  if (!process_manager_->UpdateExitCallback(
          pid, base::BindOnce(&DHCPCDProxyFactory::OnProcessExited,
                              weak_ptr_factory_.GetWeakPtr(), pid,
                              std::string(logging_tag)))) {
    return nullptr;
  }

  // Register the proxy and return it.
  auto proxy =
      std::make_unique<DHCPCDProxy>(process_manager_, interface, handler,
                                    base::ScopedClosureRunner(base::BindOnce(
                                        &DHCPCDProxyFactory::OnProxyDestroyed,
                                        weak_ptr_factory_.GetWeakPtr(), pid)),
                                    logging_tag);
  alive_proxies_.insert(std::make_pair(
      pid, AliveProxy{proxy->GetWeakPtr(), std::move(clean_up_closure)}));
  return proxy;
}

void DHCPCDProxyFactory::CleanUpDhcpcd(const std::string& interface,
                                       net_base::IPFamily family,
                                       DHCPClientProxy::Options options,
                                       int pid,
                                       const std::string& logging_tag) {
  const auto iter = pids_need_to_stop_.find(pid);
  if (iter != pids_need_to_stop_.end()) {
    // Terminate the dhcpcd process first with SIGALRM, then SIGTERM (with
    // timeout), finally SIGKILL (with timeout) signals.
    // To stop dhcpcd, SIGALRM is better than SIGTERM, for upon receiving
    // SIGALRM, dhcpcd will try to release the lease first, then exits, but for
    // SIGTERM, dhcpcd will not send the RELEASE message.
    // Although in net_base::ProcessManager::KillProcess() we still send SIGTERM
    // before sending SIGKILL, this SIGTERM actually will have no effect on
    // dhcpcd. When dhcpcd receives SIGALRM at the beginning, it sets its
    // internal static variable |dhcpcd_exiting| to true. This variable prevents
    // dhcpcd from handling all subsequent SIGTERM signals. We use KillProcess()
    // here only for waiting the dhcpcd to exit, and if the process doesn't
    // exit for within a certain time, KillProcess() kills it by force with
    // SIGKILL.
    bool killed = false;
    if (!process_manager_->KillProcess(pid, SIGALRM, &killed)) {
      LOG(WARNING) << logging_tag << " " << __func__
                   << ": Failed to send SIGALRM to pid: " << pid;
    }
    if (!killed) {
      process_manager_->StopProcessAndBlock(pid);
    }
    pids_need_to_stop_.erase(iter);
  }

  // Clean up the lease file and pid file.
  brillo::DeleteFile(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatLease, interface.c_str())));
  brillo::DeleteFile(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatPID, interface.c_str(),
                         family == net_base::IPFamily::kIPv6 ? 6 : 4)));
}

void DHCPCDProxyFactory::OnProcessExited(int pid,
                                         const std::string& logging_tag,
                                         int exit_status) {
  LOG(INFO) << logging_tag << " " << __func__
            << ": The dhcpcd process with pid " << pid
            << " is exited with status: " << exit_status;
  pids_need_to_stop_.erase(pid);

  DHCPCDProxy* proxy = GetAliveProxy(pid);
  if (proxy == nullptr) {
    return;
  }
  alive_proxies_.erase(pid);

  proxy->OnProcessExited(pid, exit_status);
}

DHCPCDProxy* DHCPCDProxyFactory::GetAliveProxy(int pid) const {
  auto iter = alive_proxies_.find(pid);
  if (iter == alive_proxies_.end()) {
    LOG(WARNING) << __func__
                 << ": Received signal from the untracked dhcpcd with pid: "
                 << pid;
    return nullptr;
  }

  base::WeakPtr<DHCPCDProxy> proxy = iter->second.proxy;
  if (!proxy) {
    LOG(INFO) << __func__ << ": The proxy with pid: " << pid
              << " is invalidated";
    return nullptr;
  }

  return proxy.get();
}

void DHCPCDProxyFactory::OnProxyDestroyed(int pid) {
  alive_proxies_.erase(pid);
}

void DHCPCDProxyFactory::OnDHCPEvent(
    const std::map<std::string, std::string>& configuration) {
  const auto iter = configuration.find(DHCPv4Config::kConfigurationKeyPid);
  if (iter == configuration.end()) {
    LOG(WARNING) << __func__ << ": " << DHCPv4Config::kConfigurationKeyPid
                 << " is missing from configuration";
    return;
  }

  int pid;
  if (!base::StringToInt(iter->second, &pid)) {
    LOG(WARNING) << __func__
                 << ": Failed to parse the pid from the configuration: "
                 << iter->second;
    return;
  }

  DHCPCDProxy* proxy = GetAliveProxy(pid);
  if (!proxy) {
    LOG(WARNING) << __func__ << ": Proxy with pid " << pid << " is not found";
    return;
  }

  proxy->OnDHCPEvent(configuration);
}

}  // namespace shill
