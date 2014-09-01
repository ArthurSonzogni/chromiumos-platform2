// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/power_manager.h"

#include <map>
#include <string>

#include <base/bind.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dbus_manager.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/power_manager_proxy_interface.h"
#include "shill/proxy_factory.h"

using base::Bind;
using base::TimeDelta;
using base::Unretained;
using std::map;
using std::string;

namespace shill {

// static
const int PowerManager::kInvalidSuspendId = -1;
const char PowerManager::kSuspendDelayDescription[] = "shill";
const int PowerManager::kSuspendTimeoutMilliseconds = 15 * 1000;

PowerManager::PowerManager(EventDispatcher *dispatcher,
                           ProxyFactory *proxy_factory)
    : dispatcher_(dispatcher),
      power_manager_proxy_(proxy_factory->CreatePowerManagerProxy(this)),
      suspend_delay_registered_(false),
      suspend_delay_id_(0),
      suspending_(false),
      current_suspend_id_(0) {}

PowerManager::~PowerManager() {}

void PowerManager::Start(
    DBusManager *dbus_manager,
    TimeDelta suspend_delay,
    const SuspendImminentCallback &suspend_imminent_callback,
    const SuspendDoneCallback &suspend_done_callback) {
  power_manager_name_watcher_.reset(
      dbus_manager->CreateNameWatcher(
          power_manager::kPowerManagerServiceName,
          Bind(&PowerManager::OnPowerManagerAppeared, Unretained(this)),
          Bind(&PowerManager::OnPowerManagerVanished, Unretained(this))));

  suspend_delay_ = suspend_delay;
  suspend_imminent_callback_ = suspend_imminent_callback;
  suspend_done_callback_ = suspend_done_callback;
}

void PowerManager::Stop() {
  LOG(INFO) << __func__;
  power_manager_name_watcher_.reset();
  // We may attempt to unregister with a stale |suspend_delay_id_| if powerd
  // reappeared behind our back. It is safe to do so.
  if (suspend_delay_registered_)
    power_manager_proxy_->UnregisterSuspendDelay(suspend_delay_id_);

  suspend_delay_registered_ = false;
}

bool PowerManager::ReportSuspendReadiness() {
  if (!suspending_) {
    LOG(INFO) << __func__ << ": Suspend attempt ("
              << current_suspend_id_ << ") not active. Ignoring signal.";
    return false;
  }
  return power_manager_proxy_->ReportSuspendReadiness(suspend_delay_id_,
                                                      current_suspend_id_);
}

void PowerManager::OnSuspendImminent(int suspend_id) {
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  current_suspend_id_ = suspend_id;

  // If we're already suspending, don't call the |suspend_imminent_callback_|
  // again.
  if (!suspending_) {
    // Change the power state to suspending as soon as this signal is received
    // so that the manager can suppress auto-connect, for example.
    // Also, we must set this before running the callback below, because the
    // callback may synchronously report suspend readiness.
    suspending_ = true;
    suspend_imminent_callback_.Run();
  }
}

void PowerManager::OnSuspendDone(int suspend_id) {
  // NB: |suspend_id| could be -1. See OnPowerManagerVanished.
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  if (!suspending_) {
    LOG(WARNING) << "Recieved unexpected SuspendDone ("
                 << suspend_id << "). Ignoring.";
    return;
  }

  suspending_ = false;
  suspend_done_callback_.Run();
}

void PowerManager::OnPowerManagerAppeared(const string &/*name*/,
                                          const string &/*owner*/) {
  LOG(INFO) << __func__;
  CHECK(!suspend_delay_registered_);
  if (power_manager_proxy_->RegisterSuspendDelay(suspend_delay_,
                                                 kSuspendDelayDescription,
                                                 &suspend_delay_id_))
    suspend_delay_registered_ = true;
}

void PowerManager::OnPowerManagerVanished(const string &/*name*/) {
  LOG(INFO) << __func__;
  // If powerd vanished during a suspend, we need to wake ourselves up.
  if (suspending_)
    OnSuspendDone(kInvalidSuspendId);
  suspend_delay_registered_ = false;
}

}  // namespace shill
