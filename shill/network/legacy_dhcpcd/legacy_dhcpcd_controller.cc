// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_controller.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/process/process_iterator.h>
#include <net-base/process_manager.h>

#include "dhcpcd/dbus-proxies.h"
#include "shill/network/dhcpcd_controller_interface.h"
#include "shill/technology.h"

namespace shill {
namespace {

constexpr char kDHCPCDExecutableName[] = "dhcpcd";
constexpr char kDHCPCDPath[] = "/sbin/dhcpcd";
constexpr char kDHCPCDUser[] = "dhcp";
constexpr char kDHCPCDGroup[] = "dhcp";

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

// Stops the dhcpcd process with |pid|.
void StopDhcpcdProcess(net_base::ProcessManager* process_manager, int pid) {
  // Pass the termination responsibility to net_base::ProcessManager.
  // net_base::ProcessManager will try to terminate the process using SIGTERM,
  // then SIGKill signals.  It will log an error message if it is not able to
  // terminate the process in a timely manner.
  process_manager->StopProcessAndBlock(pid);
}

}  // namespace

LegacyDHCPCDController::LegacyDHCPCDController(
    std::string_view interface,
    DHCPCDControllerInterface::EventHandler* handler,
    std::unique_ptr<org::chromium::dhcpcdProxy> dhcpcd_proxy,
    base::ScopedClosureRunner destroy_cb)
    : DHCPCDControllerInterface(interface, handler),
      dhcpcd_proxy_(std::move(dhcpcd_proxy)),
      destroy_cb_(std::move(destroy_cb)) {}

LegacyDHCPCDController::~LegacyDHCPCDController() = default;

bool LegacyDHCPCDController::Rebind() {
  brillo::ErrorPtr error;
  if (!dhcpcd_proxy_->Rebind(interface_, &error)) {
    LogDBusError(error, __func__, interface_);
    return false;
  }
  return true;
}

bool LegacyDHCPCDController::Release() {
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

void LegacyDHCPCDController::OnStatusChanged(Status status) {
  handler_->OnStatusChanged(status);
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

bool LegacyDHCPCDControllerFactory::CreateAsync(
    std::string_view interface,
    Technology technology,
    const DHCPCDControllerInterface::Options& options,
    DHCPCDControllerInterface::EventHandler* handler,
    CreateCB create_cb) {
  std::vector<std::string> args = GetDhcpcdFlags(technology, options);
  if (options.lease_name.empty() || options.lease_name == interface) {
    args.push_back(std::string(interface));
  } else {
    args.push_back(base::StrCat({interface, "=", options.lease_name}));
  }

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
    return false;
  }
  base::ScopedClosureRunner clean_up_closure(
      base::BindOnce(&StopDhcpcdProcess, process_manager_, pid));

  // Inject the exit callback with pid information.
  if (!process_manager_->UpdateExitCallback(
          pid, base::BindOnce(&LegacyDHCPCDControllerFactory::OnProcessExited,
                              weak_ptr_factory_.GetWeakPtr(), pid))) {
    return false;
  }

  pending_requests_.insert(std::make_pair(
      pid, PendingRequest{std::string(interface), handler, std::move(create_cb),
                          std::move(clean_up_closure)}));
  return true;
}

void LegacyDHCPCDControllerFactory::OnProcessExited(int pid, int exit_status) {
  LOG(INFO) << __func__ << ": The dhcpcd process with pid " << pid
            << " is exited with status: " << exit_status;

  // If the dhcpcd process is exited without sending any signal, return the null
  // controller.
  const auto pending_iter = pending_requests_.find(pid);
  if (pending_iter != pending_requests_.end()) {
    std::move(pending_iter->second.create_cb).Run(nullptr);
    pending_requests_.erase(pending_iter);
    return;
  }

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
  CreateControllerIfPending(service_name, pid);
  LegacyDHCPCDController* controller = GetAliveController(pid);
  if (controller == nullptr) {
    return;
  }

  controller->OnDHCPEvent(reason, configuration);
}

void LegacyDHCPCDControllerFactory::OnStatusChanged(
    std::string_view service_name,
    uint32_t pid,
    DHCPCDControllerInterface::Status status) {
  CreateControllerIfPending(service_name, pid);
  LegacyDHCPCDController* controller = GetAliveController(pid);
  if (controller == nullptr) {
    return;
  }

  controller->OnStatusChanged(status);
}

void LegacyDHCPCDControllerFactory::CreateControllerIfPending(
    std::string_view service_name, int pid) {
  auto iter = pending_requests_.find(pid);
  if (iter == pending_requests_.end()) {
    return;
  }

  LOG(INFO) << __func__ << ": Create the controller for pid: " << pid;
  auto dhcpcd_proxy = std::make_unique<org::chromium::dhcpcdProxy>(
      bus_, std::string(service_name));
  auto controller = std::make_unique<LegacyDHCPCDController>(
      iter->second.interface, iter->second.handler, std::move(dhcpcd_proxy),
      base::ScopedClosureRunner(
          base::BindOnce(&LegacyDHCPCDControllerFactory::OnControllerDestroyed,
                         weak_ptr_factory_.GetWeakPtr(), pid)));

  // Register the controller and return it by create_cb.
  alive_controllers_.insert(std::make_pair(
      pid, AliveController{controller->GetWeakPtr(),
                           std::move(iter->second.clean_up_closure)}));
  CreateCB create_cb = std::move(iter->second.create_cb);
  pending_requests_.erase(iter);
  std::move(create_cb).Run(std::move(controller));
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
