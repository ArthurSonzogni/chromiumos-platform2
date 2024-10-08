// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/action_runner.h"

#include <unistd.h>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <power_manager/dbus-proxies.h>

#include "heartd/daemon/boot_record.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

void SyncData() {
  brillo::ProcessImpl sync_process;
  sync_process.AddArg("/bin/sync");
  // We don't call `Wait()` because it may block the following flow.
  // Instead, we sleep 10 seconds for sync.
  sync_process.Start();
  sleep(10);
}

}  // namespace

ActionRunner::ActionRunner(DbusConnector* dbus_connector)
    : dbus_connector_(dbus_connector) {
  CHECK(dbus_connector_) << "DbusConnector object is nullptr.";
}

ActionRunner::~ActionRunner() {
  if (sysrq_fd_ != -1) {
    close(sysrq_fd_);
  }
}

void ActionRunner::SetupSysrq(int sysrq_fd) {
  sysrq_fd_ = sysrq_fd;
}

mojom::HeartbeatResponse ActionRunner::DryRun(mojom::ServiceName name,
                                              mojom::ActionType action) {
  switch (action) {
    case mojom::ActionType::kUnmappedEnumField:
      return mojom::HeartbeatResponse::kNotAllowed;
    case mojom::ActionType::kNoOperation:
      return mojom::HeartbeatResponse::kSuccess;
    case mojom::ActionType::kNormalReboot:
      if (!allow_normal_reboot_) {
        LOG(WARNING) << "Heartd is not allowed to normal reboot the device.";
        return mojom::HeartbeatResponse::kNotAllowed;
      }
      if (IsNormalRebootTooManyTimes()) {
        return mojom::HeartbeatResponse::kRateLimit;
      }
      break;
    case mojom::ActionType::kForceReboot:
      if (!allow_force_reboot_) {
        LOG(WARNING) << "Heartd is not allowed to force reboot the device.";
        return mojom::HeartbeatResponse::kNotAllowed;
      }
      if (IsForceRebootTooManyTimes()) {
        return mojom::HeartbeatResponse::kRateLimit;
      }
      break;
    case mojom::ActionType::kSyncData:
      return mojom::HeartbeatResponse::kSuccess;
  }
  return mojom::HeartbeatResponse::kSuccess;
}

void ActionRunner::Run(mojom::ServiceName name, mojom::ActionType action) {
  if (DryRun(name, action) == mojom::HeartbeatResponse::kSuccess) {
    switch (action) {
      case mojom::ActionType::kUnmappedEnumField:
      case mojom::ActionType::kNoOperation:
        break;
      case mojom::ActionType::kNormalReboot:
        LOG(WARNING) << "Heartd starts to reboot the device.";
        // There is nothing to do for heartd when it's success or error. When
        // failure, power manager should understand why it fails. We just need
        // to check the log.
        dbus_connector_->power_manager_proxy()->RequestRestartAsync(
            /*in_reason = REQUEST_RESTART_HEARTD */ 6,
            /*in_description = */ "heartd reset",
            /*success_callback = */ base::DoNothing(),
            /*error_callback = */ base::DoNothing());
        break;
      case mojom::ActionType::kForceReboot:
        LOG(WARNING) << "Heartd starts to force reboot the device.";
        if (sync_flag_) {
          SyncData();
        }

        if (!write(sysrq_fd_, "c", 1)) {
          LOG(ERROR) << "Heartd failed to force reboot the device";
        }
        break;
      case mojom::ActionType::kSyncData:
        if (sync_flag_) {
          SyncData();
        }
        break;
    }
  }
}

void ActionRunner::EnableNormalRebootAction() {
  allow_normal_reboot_ = true;
}

void ActionRunner::EnableForceRebootAction() {
  allow_force_reboot_ = true;
}

void ActionRunner::DisableNormalRebootAction() {
  allow_normal_reboot_ = false;
}

void ActionRunner::DisableForceRebootAction() {
  allow_force_reboot_ = false;
}

void ActionRunner::CacheBootRecord(
    const std::vector<BootRecord>& boot_records) {
  boot_records_ = boot_records;
}

int ActionRunner::GetNormalRebootCount(const base::Time& time) {
  int count = 0;
  for (int i = boot_records_.size() - 1; i >= 0; --i) {
    if (boot_records_[i].time < time) {
      break;
    }

    if (boot_records_[i].id.starts_with("shutdown.")) {
      ++count;
    }
  }

  return count;
}

int ActionRunner::GetForceRebootCount(const base::Time& time) {
  int count = 0;
  for (int i = boot_records_.size() - 2; i >= 0; --i) {
    if (boot_records_[i + 1].time < time) {
      break;
    }

    // Two consecutive boot IDs mean boot_records_[i+1] is a force reboot.
    if (!boot_records_[i].id.starts_with("shutdown.") &&
        !boot_records_[i + 1].id.starts_with("shutdown.")) {
      ++count;
    }
  }

  return count;
}

bool ActionRunner::IsNormalRebootTooManyTimes() {
  // Rules:
  // 1. At most 3 reboots in 12 hours window.
  // 2. At most 10 reboots in the 7 days window.
  if (GetNormalRebootCount(base::Time().Now() - base::Hours(12)) >= 3) {
    LOG(INFO) << "There are at least 3 normal reboot in 12 hours window";
    return true;
  }
  if (GetNormalRebootCount(base::Time().Now() - base::Days(7)) >= 10) {
    LOG(INFO) << "There are at least 10 normal reboot in 7 days window";
    return true;
  }

  return false;
}

bool ActionRunner::IsForceRebootTooManyTimes() {
  // Rules:
  // 1. At most 3 reboots in 12 hours window.
  // 2. At most 10 reboots in the 7 days window.
  if (GetForceRebootCount(base::Time().Now() - base::Hours(12)) >= 3) {
    LOG(INFO) << "There are at least 3 force reboot in 12 hours window";
    return true;
  }
  if (GetForceRebootCount(base::Time().Now() - base::Days(7)) >= 10) {
    LOG(INFO) << "There are at least 10 force reboot in 7 days window";
    return true;
  }

  return false;
}

void ActionRunner::SetSyncFlag(bool sync_flag) {
  sync_flag_ = sync_flag;
}

}  // namespace heartd
