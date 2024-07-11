// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_proxy.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/process/process_iterator.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>
#include <chromeos/net-base/process_manager.h>

#include "dhcpcd/dbus-proxies.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"
#include "shill/technology.h"

namespace shill {
namespace {

constexpr char kDHCPCDExecutableName[] = "dhcpcd7";
constexpr char kDHCPCDPath[] = "/sbin/dhcpcd7";
constexpr char kDHCPCDConfigPath[] = "/etc/dhcpcd7.conf";
constexpr char kDHCPCDUser[] = "dhcp";
constexpr char kDHCPCDGroup[] = "dhcp";
constexpr char kDHCPCDPathFormatLease[] = "var/lib/dhcpcd7/%s.lease";
constexpr char kDHCPCDPathFormatPID[] = "var/run/dhcpcd7/dhcpcd-%s-4.pid";

void LogDBusError(const brillo::ErrorPtr& error,
                  const std::string& method,
                  const std::string& interface) {
  if (error->GetCode() == DBUS_ERROR_SERVICE_UNKNOWN ||
      error->GetCode() == DBUS_ERROR_NO_REPLY) {
    LOG(INFO) << method << ": dhcpcd daemon appears to have exited.";
  } else {
    LOG(ERROR) << "DBus error: " << method << " " << interface << ": "
               << error->GetCode() << ": " << error->GetMessage();
  }
}

// Returns true if the lease file is ephemeral, which means the lease file
// should be deleted during cleanup.
bool IsEphemeralLease(const DHCPClientProxy::Options& options,
                      std::string_view interface) {
  return options.lease_name.empty() || options.lease_name == interface;
}

// Returns a list of dhcpcd args. Redacts the hostname and the lease name for
// logging if |redact_args| is set to true.
std::vector<std::string> GetDhcpcdArgs(Technology technology,
                                       const DHCPClientProxy::Options& options,
                                       std::string_view interface,
                                       bool redact_args) {
  std::vector<std::string> args = {
      "-B",                               // Run in foreground.
      "-f",        kDHCPCDConfigPath,     // Specify config file path.
      "-i",        "chromeos",            // Static value for Vendor class info.
      "-q",                               // Only warnings+errors to stderr.
      "-4",                               // IPv4 only.
      "-o",        "captive_portal_uri",  // Request the captive portal URI.
      "--nodelay",                        // No initial randomised delay.
  };

  // Request hostname from server.
  if (!options.hostname.empty()) {
    args.insert(args.end(),
                {"-h", redact_args ? "<redacted_hostname>" : options.hostname});
  }

  if (options.use_arp_gateway) {
    args.insert(args.end(), {
                                "-R",         // ARP for default gateway.
                                "--unicast",  // Enable unicast ARP on renew.
                            });
  }

  if (options.use_rfc_8925) {
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

  if (IsEphemeralLease(options, interface)) {
    args.push_back(std::string(interface));
  } else {
    args.push_back(base::StrCat(
        {interface, "=",
         redact_args ? "<redacted_lease_name>" : options.lease_name}));
  }

  return args;
}

}  // namespace

LegacyDHCPCDProxy::LegacyDHCPCDProxy(std::string_view interface,
                                     DHCPClientProxy::EventHandler* handler,
                                     base::ScopedClosureRunner destroy_cb)
    : DHCPClientProxy(interface, handler), destroy_cb_(std::move(destroy_cb)) {}

LegacyDHCPCDProxy::~LegacyDHCPCDProxy() = default;

bool LegacyDHCPCDProxy::IsReady() const {
  return dhcpcd_proxy_ != nullptr;
}

bool LegacyDHCPCDProxy::Rebind() {
  if (!dhcpcd_proxy_) {
    LOG(ERROR) << __func__ << ": dhcpcd proxy is not ready";
    return false;
  }

  brillo::ErrorPtr error;
  if (!dhcpcd_proxy_->Rebind(interface_, &error)) {
    LogDBusError(error, __func__, interface_);
    return false;
  }
  return true;
}

bool LegacyDHCPCDProxy::Release() {
  if (!dhcpcd_proxy_) {
    LOG(ERROR) << __func__ << ": dhcpcd proxy is not ready";
    return false;
  }

  brillo::ErrorPtr error;
  if (!dhcpcd_proxy_->Release(interface_, &error)) {
    LogDBusError(error, __func__, interface_);
    return false;
  }
  return true;
}

void LegacyDHCPCDProxy::OnDHCPEvent(EventReason reason,
                                    const KeyValueStore& configuration) {
  net_base::NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;

  if (NeedConfiguration(reason) &&
      !DHCPv4Config::ParseConfiguration(configuration, &network_config,
                                        &dhcp_data)) {
    LOG(WARNING) << __func__
                 << ": Error parsing network configuration from DHCP client. "
                 << "The following configuration might be partial: "
                 << network_config;
  }
  handler_->OnDHCPEvent(reason, network_config, dhcp_data);
}

base::WeakPtr<LegacyDHCPCDProxy> LegacyDHCPCDProxy::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

LegacyDHCPCDProxyFactory::LegacyDHCPCDProxyFactory(
    EventDispatcher* dispatcher,
    scoped_refptr<dbus::Bus> bus,
    net_base::ProcessManager* process_manager,
    std::unique_ptr<LegacyDHCPCDListenerFactory> listener_factory)
    : process_manager_(process_manager), bus_(std::move(bus)) {
  // Kill the dhcpcd processes accidentally left by previous run.
  base::NamedProcessIterator iter(kDHCPCDExecutableName, nullptr);
  while (const base::ProcessEntry* entry = iter.NextProcessEntry()) {
    process_manager_->StopProcessAndBlock(entry->pid());
  }

  listener_ = listener_factory->Create(
      bus_, dispatcher,
      // base::Unretained(this) is safe because |listener_| is owned by |*this|.
      base::BindRepeating(&LegacyDHCPCDProxyFactory::OnDHCPEvent,
                          base::Unretained(this)),
      base::BindRepeating(&LegacyDHCPCDProxyFactory::OnStatusChanged,
                          base::Unretained(this)));
}

LegacyDHCPCDProxyFactory::~LegacyDHCPCDProxyFactory() {
  // Clear all the alive dhcpcd processes.
  alive_proxies_.clear();
  CHECK(pids_need_to_stop_.empty());
}

std::unique_ptr<DHCPClientProxy> LegacyDHCPCDProxyFactory::Create(
    std::string_view interface,
    Technology technology,
    const DHCPClientProxy::Options& options,
    DHCPClientProxy::EventHandler* handler) {
  const std::vector<std::string> args =
      GetDhcpcdArgs(technology, options, interface, /*redact_args=*/false);

  net_base::ProcessManager::MinijailOptions minijail_options;
  minijail_options.user = kDHCPCDUser;
  minijail_options.group = kDHCPCDGroup;
  minijail_options.capmask =
      CAP_TO_MASK(CAP_NET_BIND_SERVICE) | CAP_TO_MASK(CAP_NET_BROADCAST) |
      CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  minijail_options.inherit_supplementary_groups = false;

  const pid_t pid = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kDHCPCDPath), args, {}, minijail_options,
      base::DoNothing());
  if (pid < 0) {
    LOG(ERROR) << __func__ << ": Failed to start the dhcpcd process";
    return nullptr;
  }
  pids_need_to_stop_.insert(pid);
  base::ScopedClosureRunner clean_up_closure(base::BindOnce(
      // base::Unretained(this) is safe because the closure won't be passed
      // outside this instance.
      &LegacyDHCPCDProxyFactory::CleanUpDhcpcd, base::Unretained(this),
      std::string(interface), options, pid));

  // Log dhcpcd args but redact the args to exclude PII.
  LOG(INFO) << "Created dhcpcd with pid " << pid << " and args: "
            << base::JoinString(GetDhcpcdArgs(technology, options, interface,
                                              /*redact_args=*/true),
                                " ");

  // Inject the exit callback with pid information.
  if (!process_manager_->UpdateExitCallback(
          pid, base::BindOnce(&LegacyDHCPCDProxyFactory::OnProcessExited,
                              weak_ptr_factory_.GetWeakPtr(), pid))) {
    return nullptr;
  }

  // Register the proxy and return it.
  auto proxy = std::make_unique<LegacyDHCPCDProxy>(
      interface, handler,
      base::ScopedClosureRunner(
          base::BindOnce(&LegacyDHCPCDProxyFactory::OnProxyDestroyed,
                         weak_ptr_factory_.GetWeakPtr(), pid)));
  alive_proxies_.insert(std::make_pair(
      pid, AliveProxy{proxy->GetWeakPtr(), std::move(clean_up_closure)}));
  return proxy;
}

void LegacyDHCPCDProxyFactory::CleanUpDhcpcd(const std::string& interface,
                                             DHCPClientProxy::Options options,
                                             int pid) {
  const auto iter = pids_need_to_stop_.find(pid);
  if (iter != pids_need_to_stop_.end()) {
    // Pass the termination responsibility to net_base::ProcessManager.
    // net_base::ProcessManager will try to terminate the process using SIGTERM,
    // then SIGKill signals.  It will log an error message if it is not able to
    // terminate the process in a timely manner.
    process_manager_->StopProcessAndBlock(pid);
    pids_need_to_stop_.erase(iter);
  }

  // Clean up the lease file and pid file.
  if (IsEphemeralLease(options, interface)) {
    brillo::DeleteFile(root_.Append(
        base::StringPrintf(kDHCPCDPathFormatLease, interface.c_str())));
  }
  brillo::DeleteFile(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatPID, interface.c_str())));
}

void LegacyDHCPCDProxyFactory::OnProcessExited(int pid, int exit_status) {
  LOG(INFO) << __func__ << ": The dhcpcd process with pid " << pid
            << " is exited with status: " << exit_status;
  pids_need_to_stop_.erase(pid);

  LegacyDHCPCDProxy* proxy = GetAliveProxy(pid);
  if (proxy == nullptr) {
    return;
  }
  alive_proxies_.erase(pid);

  proxy->OnProcessExited(pid, exit_status);
}

void LegacyDHCPCDProxyFactory::OnDHCPEvent(std::string_view service_name,
                                           uint32_t pid,
                                           DHCPClientProxy::EventReason reason,
                                           const KeyValueStore& configuration) {
  LegacyDHCPCDProxy* proxy = GetAliveProxy(pid);
  if (proxy == nullptr) {
    return;
  }
  SetDBusProxyIfPending(proxy, service_name, pid);

  proxy->OnDHCPEvent(reason, configuration);
}

void LegacyDHCPCDProxyFactory::OnStatusChanged(
    std::string_view service_name,
    uint32_t pid,
    LegacyDHCPCDListener::Status status) {
  LegacyDHCPCDProxy* proxy = GetAliveProxy(pid);
  if (proxy == nullptr) {
    return;
  }
  SetDBusProxyIfPending(proxy, service_name, pid);

  if (status == LegacyDHCPCDListener::Status::kIPv6OnlyPreferred) {
    proxy->OnDHCPEvent(DHCPClientProxy::EventReason::kIPv6OnlyPreferred, {});
  }
}

void LegacyDHCPCDProxyFactory::SetDBusProxyIfPending(
    LegacyDHCPCDProxy* proxy, std::string_view service_name, int pid) {
  if (proxy->IsReady()) {
    return;
  }

  LOG(INFO) << __func__
            << ": Set the D-Bus proxy to LegacyDHCPCDProxy for pid: " << pid;
  proxy->set_dhcpcd_proxy(std::make_unique<org::chromium::dhcpcdProxy>(
      bus_, std::string(service_name)));
}

LegacyDHCPCDProxy* LegacyDHCPCDProxyFactory::GetAliveProxy(int pid) const {
  auto iter = alive_proxies_.find(pid);
  if (iter == alive_proxies_.end()) {
    LOG(WARNING) << "Received signal from the untracked dhcpcd with pid: "
                 << pid;
    return nullptr;
  }

  base::WeakPtr<LegacyDHCPCDProxy> proxy = iter->second.proxy;
  if (!proxy) {
    LOG(INFO) << "The proxy with pid: " << pid << " is invalidated";
    return nullptr;
  }

  return proxy.get();
}

void LegacyDHCPCDProxyFactory::OnProxyDestroyed(int pid) {
  alive_proxies_.erase(pid);
}

}  // namespace shill
