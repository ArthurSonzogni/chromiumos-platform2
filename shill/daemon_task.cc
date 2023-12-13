// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/daemon_task.h"

#include <memory>
#include <utility>

#include <linux/rtnetlink.h>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/message_loops/message_loop.h>
#include <net-base/netlink_manager.h>
#include <net-base/netlink_message.h>

#include "shill/control_interface.h"
#include "shill/dbus/dbus_control.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/mojom/mojo_service_provider.h"
#include "shill/net/process_manager.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/network_applier.h"
#include "shill/shill_config.h"
#include "shill/wifi/nl80211_message.h"

namespace shill {

// Netlink multicast group for neighbor discovery user option message.
// The first valid index is defined as enum value 1, so we need to -1 offset.
constexpr uint32_t RTMGRP_ND_USEROPT = 1 << (RTNLGRP_ND_USEROPT - 1);

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDaemon;
}  // namespace Logging

DaemonTask::DaemonTask(const Settings& settings, Config* config)
    : settings_(settings),
      config_(config),
      rtnl_handler_(nullptr),
      network_applier_(nullptr),
      dhcp_provider_(nullptr),
      netlink_manager_(nullptr),
      process_manager_(nullptr) {}

DaemonTask::~DaemonTask() = default;

void DaemonTask::ApplySettings() {
  manager_->SetBlockedDevices(settings_.devices_blocked);
  manager_->SetAllowedDevices(settings_.devices_allowed);
  manager_->SetIgnoreUnknownEthernet(settings_.ignore_unknown_ethernet);
}

bool DaemonTask::Quit(base::OnceClosure completion_callback) {
  SLOG(1) << "Starting termination actions.";
  if (manager_->RunTerminationActionsAndNotifyMetrics(base::BindOnce(
          &DaemonTask::TerminationActionsCompleted, base::Unretained(this)))) {
    SLOG(1) << "Will wait for termination actions to complete";
    termination_completed_callback_ = std::move(completion_callback);
    return false;  // Note to caller: don't exit yet!
  } else {
    SLOG(1) << "No termination actions were run";
    StopAndReturnToMain();
    return true;  // All done, ready to exit.
  }
}

void DaemonTask::Init() {
  dispatcher_.reset(new EventDispatcher());
  control_.reset(new DBusControl(dispatcher_.get()));
  metrics_.reset(new Metrics());
  rtnl_handler_ = net_base::RTNLHandler::GetInstance();
  network_applier_ = NetworkApplier::GetInstance();
  dhcp_provider_ = DHCPProvider::GetInstance();
  process_manager_ = ProcessManager::GetInstance();
  netlink_manager_ = net_base::NetlinkManager::GetInstance();
  manager_.reset(new Manager(control_.get(), dispatcher_.get(), metrics_.get(),
                             config_->GetRunDirectory(),
                             config_->GetStorageDirectory(),
                             config_->GetUserStorageDirectory()));
  control_->RegisterManagerObject(
      manager_.get(),
      base::BindOnce(&DaemonTask::Start, base::Unretained(this)));
  mojo_provider_ = std::make_unique<MojoServiceProvider>(manager_.get());
  ApplySettings();
}

void DaemonTask::TerminationActionsCompleted(const Error& error) {
  SLOG(1) << "Finished termination actions.  Result: " << error;
  // Daemon::TerminationActionsCompleted() should not directly call
  // Daemon::Stop(). Otherwise, it could lead to the call sequence below. That
  // is not safe as the HookTable's start callback only holds a weak pointer to
  // the Cellular object, which is destroyed in midst of the
  // Cellular::OnTerminationCompleted() call. We schedule the
  // Daemon::StopAndReturnToMain() call through the message loop instead.
  //
  // Daemon::Quit
  //   -> Manager::RunTerminationActionsAndNotifyMetrics
  //     -> Manager::RunTerminationActions
  //       -> HookTable::Run
  //         ...
  //         -> Cellular::OnTerminationCompleted
  //           -> Manager::TerminationActionComplete
  //             -> HookTable::ActionComplete
  //               -> Daemon::TerminationActionsCompleted
  //                 -> Daemon::Stop
  //                   -> Manager::Stop
  //                     -> DeviceInfo::Stop
  //                       -> Cellular::~Cellular
  //           -> Manager::RemoveTerminationAction
  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(&DaemonTask::StopAndReturnToMain, base::Unretained(this)));
}

void DaemonTask::StopAndReturnToMain() {
  Stop();
  if (!termination_completed_callback_.is_null()) {
    std::move(termination_completed_callback_).Run();
  }
}

void DaemonTask::Start() {
  rtnl_handler_->Start(RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
                       RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE |
                       RTMGRP_ND_USEROPT);
  network_applier_->Start();
  dhcp_provider_->Init(control_.get(), dispatcher_.get(), metrics_.get());
  process_manager_->Init();
  // Note that net_base::NetlinkManager initialization is not necessarily
  // WiFi-specific. It just happens that we currently only use
  // net_base::NetlinkManager for WiFi.
  if (netlink_manager_) {
    netlink_manager_->Init();
    uint16_t nl80211_family_id = netlink_manager_->GetFamily(
        Nl80211Message::kMessageTypeString,
        base::BindRepeating(&Nl80211Message::CreateMessage));
    if (nl80211_family_id == net_base::NetlinkMessage::kIllegalMessageType) {
      LOG(FATAL) << "Didn't get a legal message type for 'nl80211' messages.";
    }
    Nl80211Message::SetMessageType(nl80211_family_id);
    netlink_manager_->Start();
  }
  manager_->Start();
  mojo_provider_->Start();
}

void DaemonTask::Stop() {
  mojo_provider_->Stop();
  mojo_provider_ = nullptr;
  manager_->Stop();
  manager_ = nullptr;  // Release manager resources, including DBus adaptor.
  dhcp_provider_->Stop();
  process_manager_->Stop();
  metrics_ = nullptr;
  // Must retain |control_|, as the D-Bus library may
  // have some work left to do. See crbug.com/537771.
}

void DaemonTask::BreakTerminationLoop() {
  // Break out of the termination loop, to continue on with other shutdown
  // tasks.
  brillo::MessageLoop::current()->BreakLoop();
}

}  // namespace shill
