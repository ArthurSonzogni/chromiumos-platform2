// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpcd_proxy.h"

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
#include <brillo/files/file_util.h>
#include <chromeos/net-base/process_manager.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/technology.h"

namespace shill {
namespace {

constexpr char kDHCPCDExecutableName[] = "dhcpcd";
constexpr char kDHCPCDPath[] = "/sbin/dhcpcd";
constexpr char kDHCPCDConfigPath[] = "/etc/dhcpcd.conf";
constexpr char kDHCPCDUser[] = "dhcp";
constexpr char kDHCPCDGroup[] = "dhcp";
constexpr char kDHCPCDPathFormatLease[] = "var/lib/dhcpcd/%s.lease";
constexpr char kDHCPCDPathFormatPID[] = "var/run/dhcpcd/dhcpcd-%s-4.pid";

std::vector<std::string> GetDhcpcdFlags(
    Technology technology, const DHCPClientProxy::Options& options) {
  std::vector<std::string> flags = {
      // Run in foreground.
      "-B",
      // Specify config file path.
      "-f",
      kDHCPCDConfigPath,
      // Static value for Vendor class info.
      "-i",
      "chromeos",
      // Only warnings+errors to stderr.
      "-q",
      // IPv4 only.
      "-4",
      // Request the captive portal URI.
      "-o",
      "captive_portal_uri",
      // No initial randomised delay.
      "--nodelay",
      // Do not configure the system.
      "--noconfigure",
  };

  // Request hostname from server.
  if (!options.hostname.empty()) {
    flags.insert(flags.end(), {"-h", options.hostname});
  }

  if (options.use_arp_gateway) {
    flags.insert(flags.end(), {
                                  "-R",         // ARP for default gateway.
                                  "--unicast",  // Enable unicast ARP on renew.
                              });
  }

  if (options.use_rfc_8925) {
    // Request option 108 to prefer IPv6-only. If server also supports this, no
    // dhcp lease will be assigned and dhcpcd will notify shill with an
    // IPv6OnlyPreferred StatusChanged event.
    flags.insert(flags.end(), {"-o", "ipv6_only_preferred"});
  }

  // TODO(jiejiang): This will also include the WiFi Direct GC mode now. We may
  // want to check if we should enable it in the future.
  if (options.apply_dscp && technology == Technology::kWiFi) {
    // This flag is added by https://crrev.com/c/4861699.
    flags.push_back("--apply_dscp");
  }

  return flags;
}

// Returns true if the lease file is ephermeral, which means the lease file
// should be deleted during cleanup.
bool IsEphemeralLease(const DHCPClientProxy::Options& options,
                      std::string_view interface) {
  return options.lease_name.empty() || options.lease_name == interface;
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

}  // namespace

DHCPCDProxy::DHCPCDProxy(net_base::ProcessManager* process_manager,
                         std::string_view interface,
                         DHCPClientProxy::EventHandler* handler,
                         base::ScopedClosureRunner destroy_cb)
    : DHCPClientProxy(interface, handler),
      process_manager_(process_manager),
      destroy_cb_(std::move(destroy_cb)) {}

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
    LOG(ERROR) << __func__ << ": Failed to run dhcpcd with args:"
               << base::JoinString(args, " ");
    return false;
  }

  return true;
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
    DHCPClientProxy::EventHandler* handler) {
  std::vector<std::string> args = GetDhcpcdFlags(technology, options);
  if (IsEphemeralLease(options, interface)) {
    args.push_back(std::string(interface));
  } else {
    args.push_back(base::StrCat({interface, "=", options.lease_name}));
  }

  const pid_t pid =
      RunDHCPCDInMinijail(process_manager_, args, /*need_cap=*/true);
  if (pid == net_base::ProcessManager::kInvalidPID) {
    LOG(ERROR) << __func__ << ": Failed to start the dhcpcd process";
    return nullptr;
  }
  pids_need_to_stop_.insert(pid);
  base::ScopedClosureRunner clean_up_closure(base::BindOnce(
      // base::Unretained(this) is safe because the closure won't be passed
      // outside this instance.
      &DHCPCDProxyFactory::CleanUpDhcpcd, base::Unretained(this),
      std::string(interface), options, pid));

  // Inject the exit callback with pid information.
  if (!process_manager_->UpdateExitCallback(
          pid, base::BindOnce(&DHCPCDProxyFactory::OnProcessExited,
                              weak_ptr_factory_.GetWeakPtr(), pid))) {
    return nullptr;
  }

  // Register the proxy and return it.
  auto proxy =
      std::make_unique<DHCPCDProxy>(process_manager_, interface, handler,
                                    base::ScopedClosureRunner(base::BindOnce(
                                        &DHCPCDProxyFactory::OnProxyDestroyed,
                                        weak_ptr_factory_.GetWeakPtr(), pid)));
  alive_proxies_.insert(std::make_pair(
      pid, AliveProxy{proxy->GetWeakPtr(), std::move(clean_up_closure)}));
  return proxy;
}

void DHCPCDProxyFactory::CleanUpDhcpcd(const std::string& interface,
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

void DHCPCDProxyFactory::OnProcessExited(int pid, int exit_status) {
  LOG(INFO) << __func__ << ": The dhcpcd process with pid " << pid
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
    LOG(WARNING) << "Received signal from the untracked dhcpcd with pid: "
                 << pid;
    return nullptr;
  }

  base::WeakPtr<DHCPCDProxy> proxy = iter->second.proxy;
  if (!proxy) {
    LOG(INFO) << "The proxy with pid: " << pid << " is invalidated";
    return nullptr;
  }

  return proxy.get();
}

void DHCPCDProxyFactory::OnProxyDestroyed(int pid) {
  alive_proxies_.erase(pid);
}

}  // namespace shill
