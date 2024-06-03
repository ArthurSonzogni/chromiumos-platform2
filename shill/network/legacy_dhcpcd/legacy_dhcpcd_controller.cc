// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_controller.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <brillo/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/process/process_iterator.h>
#include <net-base/process_manager.h>

#include "dhcpcd/dbus-proxies.h"
#include "shill/network/dhcpcd_controller_interface.h"
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

std::vector<std::string> GetDhcpcdFlags(
    Technology technology, const DHCPCDControllerInterface::Options& options) {
  std::vector<std::string> flags = {
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

}  // namespace

LegacyDHCPCDController::LegacyDHCPCDController(
    std::string_view interface,
    DHCPCDControllerInterface::EventHandler* handler,
    base::ScopedClosureRunner destroy_cb)
    : DHCPCDControllerInterface(interface, handler),
      destroy_cb_(std::move(destroy_cb)) {}

LegacyDHCPCDController::~LegacyDHCPCDController() = default;

bool LegacyDHCPCDController::IsReady() const {
  return dhcpcd_proxy_ != nullptr;
}

bool LegacyDHCPCDController::Rebind() {
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

bool LegacyDHCPCDController::Release() {
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

void LegacyDHCPCDController::OnDHCPEvent(EventReason reason,
                                         const KeyValueStore& configuration) {
  handler_->OnDHCPEvent(reason, configuration);
}

base::WeakPtr<LegacyDHCPCDController> LegacyDHCPCDController::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

LegacyDHCPCDControllerFactory::LegacyDHCPCDControllerFactory(
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
      base::BindRepeating(&LegacyDHCPCDControllerFactory::OnDHCPEvent,
                          base::Unretained(this)),
      base::BindRepeating(&LegacyDHCPCDControllerFactory::OnStatusChanged,
                          base::Unretained(this)));
}

LegacyDHCPCDControllerFactory::~LegacyDHCPCDControllerFactory() = default;

std::unique_ptr<DHCPCDControllerInterface>
LegacyDHCPCDControllerFactory::Create(
    std::string_view interface,
    Technology technology,
    const DHCPCDControllerInterface::Options& options,
    DHCPCDControllerInterface::EventHandler* handler) {
  std::vector<std::string> args = GetDhcpcdFlags(technology, options);
  args.push_back(std::string(interface));

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
  base::ScopedClosureRunner clean_up_closure(base::BindOnce(
      // base::Unretained(this) is safe because the closure won't be passed
      // outside this instance.
      &LegacyDHCPCDControllerFactory::CleanUpDhcpcd, base::Unretained(this),
      std::string(interface), options, pid));

  // Inject the exit callback with pid information.
  if (!process_manager_->UpdateExitCallback(
          pid, base::BindOnce(&LegacyDHCPCDControllerFactory::OnProcessExited,
                              weak_ptr_factory_.GetWeakPtr(), pid))) {
    return nullptr;
  }

  // Register the controller and return it.
  auto controller = std::make_unique<LegacyDHCPCDController>(
      interface, handler,
      base::ScopedClosureRunner(
          base::BindOnce(&LegacyDHCPCDControllerFactory::OnControllerDestroyed,
                         weak_ptr_factory_.GetWeakPtr(), pid)));
  alive_controllers_.insert(std::make_pair(
      pid,
      AliveController{controller->GetWeakPtr(), std::move(clean_up_closure)}));
  return controller;
}

void LegacyDHCPCDControllerFactory::CleanUpDhcpcd(
    const std::string& interface,
    DHCPCDControllerInterface::Options options,
    int pid) {
  // Pass the termination responsibility to net_base::ProcessManager.
  // net_base::ProcessManager will try to terminate the process using SIGTERM,
  // then SIGKill signals.  It will log an error message if it is not able to
  // terminate the process in a timely manner.
  process_manager_->StopProcessAndBlock(pid);

  // Clean up the lease file and pid file.
  brillo::DeleteFile(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatLease, interface.c_str())));
  brillo::DeleteFile(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatPID, interface.c_str())));
}

void LegacyDHCPCDControllerFactory::OnProcessExited(int pid, int exit_status) {
  LOG(INFO) << __func__ << ": The dhcpcd process with pid " << pid
            << " is exited with status: " << exit_status;

  LegacyDHCPCDController* controller = GetAliveController(pid);
  if (controller == nullptr) {
    return;
  }
  alive_controllers_.erase(pid);

  controller->OnProcessExited(pid, exit_status);
}

void LegacyDHCPCDControllerFactory::OnDHCPEvent(
    std::string_view service_name,
    uint32_t pid,
    DHCPCDControllerInterface::EventReason reason,
    const KeyValueStore& configuration) {
  LegacyDHCPCDController* controller = GetAliveController(pid);
  if (controller == nullptr) {
    return;
  }
  SetProxyToControllerIfPending(controller, service_name, pid);

  controller->OnDHCPEvent(reason, configuration);
}

void LegacyDHCPCDControllerFactory::OnStatusChanged(
    std::string_view service_name,
    uint32_t pid,
    LegacyDHCPCDListener::Status status) {
  LegacyDHCPCDController* controller = GetAliveController(pid);
  if (controller == nullptr) {
    return;
  }
  SetProxyToControllerIfPending(controller, service_name, pid);

  if (status == LegacyDHCPCDListener::Status::kIPv6OnlyPreferred) {
    controller->OnDHCPEvent(
        DHCPCDControllerInterface::EventReason::kIPv6OnlyPreferred, {});
  }
}

void LegacyDHCPCDControllerFactory::SetProxyToControllerIfPending(
    LegacyDHCPCDController* controller,
    std::string_view service_name,
    int pid) {
  if (controller->IsReady()) {
    return;
  }

  LOG(INFO) << __func__ << ": Set the proxy to the controller for pid: " << pid;
  controller->set_dhcpcd_proxy(std::make_unique<org::chromium::dhcpcdProxy>(
      bus_, std::string(service_name)));
}

LegacyDHCPCDController* LegacyDHCPCDControllerFactory::GetAliveController(
    int pid) const {
  auto iter = alive_controllers_.find(pid);
  if (iter == alive_controllers_.end()) {
    LOG(WARNING) << "Received signal from the untracked dhcpcd with pid: "
                 << pid;
    return nullptr;
  }

  base::WeakPtr<LegacyDHCPCDController> controller = iter->second.controller;
  if (!controller) {
    LOG(INFO) << "The controller with pid: " << pid << " is invalidated";
    return nullptr;
  }

  return controller.get();
}

void LegacyDHCPCDControllerFactory::OnControllerDestroyed(int pid) {
  alive_controllers_.erase(pid);
}

}  // namespace shill
