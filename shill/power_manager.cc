// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/power_manager.h"

#include <linux/nl80211.h>

#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/power_manager_proxy_interface.h"

namespace shill {

// static
const int PowerManager::kInvalidSuspendId = -1;
const char PowerManager::kSuspendDelayDescription[] = "shill";
const char PowerManager::kDarkSuspendDelayDescription[] = "shill";

PowerManager::PowerManager(ControlInterface* control_interface)
    : control_interface_(control_interface),
      delay_registration_started_(false),
      suspending_(false),
      suspend_ready_(false),
      suspend_done_deferred_(false),
      in_dark_resume_(false),
      current_suspend_id_(0),
      current_dark_suspend_id_(0),
      suspend_duration_us_(0),
      wifi_reg_domain_(std::nullopt) {}

PowerManager::~PowerManager() = default;

void PowerManager::Start(
    base::TimeDelta suspend_delay,
    const SuspendImminentCallback& suspend_imminent_callback,
    const SuspendDoneCallback& suspend_done_callback,
    const DarkSuspendImminentCallback& dark_suspend_imminent_callback) {
  power_manager_proxy_ = control_interface_->CreatePowerManagerProxy(
      this,
      base::BindRepeating(&PowerManager::OnPowerManagerAppeared,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&PowerManager::OnPowerManagerVanished,
                          weak_ptr_factory_.GetWeakPtr()));
  suspend_delay_ = suspend_delay;
  suspend_imminent_callback_ = suspend_imminent_callback;
  suspend_done_callback_ = suspend_done_callback;
  dark_suspend_imminent_callback_ = dark_suspend_imminent_callback;
}

void PowerManager::Stop() {
  LOG(INFO) << __func__;

  if (!power_manager_proxy_) {
    return;
  }

  // We may attempt to unregister with a stale |suspend_delay_id_| if powerd
  // disappeared and reappeared behind our back. It is safe to do so.
  if (suspend_delay_id_.has_value() &&
      power_manager_proxy_->UnregisterSuspendDelay(*suspend_delay_id_)) {
    suspend_delay_id_.reset();
  }
  if (dark_suspend_delay_id_.has_value() &&
      power_manager_proxy_->UnregisterDarkSuspendDelay(
          *dark_suspend_delay_id_)) {
    dark_suspend_delay_id_.reset();
  }

  power_manager_proxy_.reset();
}

void PowerManager::ReportSuspendReadiness(
    base::OnceCallback<void(bool)> callback) {
  // If |suspend_done_deferred_| is true, a SuspendDone notification was
  // observed before SuspendReadiness was reported and no further
  // SuspendImminent notification was observed after the SuspendDone
  // notification. We don't need to report SuspendReadiness, but instead notify
  // the deferred SuspendDone.
  if (suspend_done_deferred_) {
    LOG(INFO) << __func__ << ": Notifying deferred SuspendDone.";
    NotifySuspendDone();
    std::move(callback).Run(false);
    return;
  }

  suspend_ready_ = true;
  if (!suspending_) {
    LOG(INFO) << __func__ << ": Suspend attempt (" << current_suspend_id_
              << ") not active. Ignoring signal.";
    std::move(callback).Run(false);
    return;
  }

  if (!suspend_delay_id_.has_value()) {
    LOG(INFO) << __func__
              << ": No suspend delay is registered. Ignoring signal.";
    std::move(callback).Run(false);
    return;
  }
  power_manager_proxy_->ReportSuspendReadiness(
      suspend_delay_id_.value(), current_suspend_id_, std::move(callback));
}

void PowerManager::ReportDarkSuspendReadiness(
    base::OnceCallback<void(bool)> callback) {
  if (!dark_suspend_delay_id_.has_value()) {
    LOG(INFO) << __func__
              << ": No dark suspend delay is registered. Ignoring signal.";
    std::move(callback).Run(false);
    return;
  }
  power_manager_proxy_->ReportDarkSuspendReadiness(
      dark_suspend_delay_id_.value(), current_dark_suspend_id_,
      std::move(callback));
}

bool PowerManager::RecordDarkResumeWakeReason(const std::string& wake_reason) {
  return power_manager_proxy_->RecordDarkResumeWakeReason(wake_reason);
}

void PowerManager::ChangeRegDomain(nl80211_dfs_regions domain) {
  auto new_domain = power_manager::WIFI_REG_DOMAIN_NONE;
  switch (domain) {
    case NL80211_DFS_FCC:
      new_domain = power_manager::WIFI_REG_DOMAIN_FCC;
      break;
    case NL80211_DFS_ETSI:
      new_domain = power_manager::WIFI_REG_DOMAIN_EU;
      break;
    case NL80211_DFS_JP:
      new_domain = power_manager::WIFI_REG_DOMAIN_REST_OF_WORLD;
      break;
    case NL80211_DFS_UNSET:
      new_domain = power_manager::WIFI_REG_DOMAIN_NONE;
      break;
    default:
      LOG(WARNING) << "Unrecognized WiFi reg domain: "
                   << std::to_string(domain);
      return;
  }

  if (wifi_reg_domain_.has_value() && new_domain == *wifi_reg_domain_) {
    return;
  }
  wifi_reg_domain_ = new_domain;

  if (power_manager_proxy_) {
    power_manager_proxy_->ChangeRegDomain(*wifi_reg_domain_);
  }
}

void PowerManager::OnSuspendImminent(int suspend_id) {
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  current_suspend_id_ = suspend_id;

  // Ignore any previously deferred SuspendDone notification as we're going to
  // suspend again and expect a new SuspendDone notification later.
  suspend_done_deferred_ = false;

  // If we're already suspending, don't call the |suspend_imminent_callback_|
  // again.
  if (!suspending_) {
    // Change the power state to suspending as soon as this signal is received
    // so that the manager can suppress auto-connect, for example.
    // Also, we must set this before running the callback below, because the
    // callback may synchronously report suspend readiness.
    suspending_ = true;
    suspend_duration_us_ = 0;
    suspend_imminent_callback_.Run();
  }
}

void PowerManager::OnSuspendDone(int suspend_id, int64_t suspend_duration_us) {
  // NB: |suspend_id| could be -1. See OnPowerManagerVanished.
  LOG(INFO) << __func__ << "(" << suspend_id << ", "
            << base::Microseconds(suspend_duration_us_) << ")";
  if (!suspending_) {
    LOG(WARNING) << __func__ << ": Ignoring unexpected SuspendDone suspend_id: "
                 << suspend_id;
    return;
  }

  suspend_duration_us_ = suspend_duration_us;

  if (!suspend_ready_) {
    LOG(INFO) << __func__ << ": Received SuspendDone (" << suspend_id
              << ") before SuspendReadiness is reported. "
              << "Defer SuspendDone notification.";
    suspend_done_deferred_ = true;
    return;
  }

  NotifySuspendDone();
}

void PowerManager::NotifySuspendDone() {
  suspending_ = false;
  suspend_ready_ = false;
  suspend_done_deferred_ = false;
  in_dark_resume_ = false;
  suspend_done_callback_.Run();
}

void PowerManager::OnDarkSuspendImminent(int suspend_id) {
  LOG(INFO) << __func__ << "(" << suspend_id << ")";
  if (!dark_suspend_delay_id_.has_value()) {
    LOG(WARNING) << __func__
                 << ": Ignoring DarkSuspendImminent signal from powerd. shill "
                 << "does not have a dark suspend delay registered. This "
                 << "means that shill is not guaranteed any time before a "
                 << "resuspend.";
    return;
  }

  in_dark_resume_ = true;
  current_dark_suspend_id_ = suspend_id;
  dark_suspend_imminent_callback_.Run();
}

void PowerManager::OnPowerManagerAppeared() {
  LOG(INFO) << __func__;

  // This function could get called twice in a row due to races in
  // ObjectProxy.
  if (delay_registration_started_) {
    return;
  }

  delay_registration_started_ = true;

  power_manager_proxy_->RegisterSuspendDelay(
      suspend_delay_, kSuspendDelayDescription,
      base::BindOnce(&PowerManager::OnSuspendDelayRegistered,
                     weak_ptr_factory_.GetWeakPtr()));

  power_manager_proxy_->RegisterDarkSuspendDelay(
      suspend_delay_, kDarkSuspendDelayDescription,
      base::BindOnce(&PowerManager::OnDarkSuspendDelayRegistered,
                     weak_ptr_factory_.GetWeakPtr()));

  if (wifi_reg_domain_.has_value()) {
    power_manager_proxy_->ChangeRegDomain(*wifi_reg_domain_);
  }
}

void PowerManager::OnSuspendDelayRegistered(std::optional<int> delay_id) {
  DCHECK(!suspend_delay_id_.has_value());
  suspend_delay_id_ = delay_id;
}

void PowerManager::OnDarkSuspendDelayRegistered(std::optional<int> delay_id) {
  DCHECK(!dark_suspend_delay_id_.has_value());
  dark_suspend_delay_id_ = delay_id;
}

void PowerManager::OnPowerManagerVanished() {
  LOG(INFO) << __func__;
  // If powerd vanished during a suspend, we need to wake ourselves up.
  if (suspending_) {
    suspend_ready_ = true;
    OnSuspendDone(kInvalidSuspendId, 0);
  }

  suspend_delay_id_.reset();
  dark_suspend_delay_id_.reset();
  delay_registration_started_ = false;
}

}  // namespace shill
