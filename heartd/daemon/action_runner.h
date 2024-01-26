// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_ACTION_RUNNER_H_
#define HEARTD_DAEMON_ACTION_RUNNER_H_

#include <vector>

#include <base/time/time.h>

#include "heartd/daemon/boot_record.h"
#include "heartd/daemon/dbus_connector.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

class ActionRunner {
 public:
  explicit ActionRunner(DbusConnector* dbus_connector);
  ActionRunner(const ActionRunner&) = delete;
  ActionRunner& operator=(const ActionRunner&) = delete;
  ~ActionRunner();

  void SetupSysrq(int sysrq_fd);

  // DryRuns the |action| for service |name|. Will be used by ping to give
  // feedback to requester about rate limits and other errors.
  ::ash::heartd::mojom::HeartbeatResponse DryRun(
      ::ash::heartd::mojom::ServiceName name,
      ::ash::heartd::mojom::ActionType action);

  // Run the |action| for service |name|.
  void Run(::ash::heartd::mojom::ServiceName name,
           ::ash::heartd::mojom::ActionType action);

  // Enables the normal reboot action.
  void EnableNormalRebootAction();

  // Enables the force reboot action.
  void EnableForceRebootAction();

  // Disable the normal reboot action.
  void DisableNormalRebootAction();

  // Disable the force reboot action.
  void DisableForceRebootAction();

  // Cache the BootRecord result to prevent unlimited reboot action.
  void CacheBootRecord(const std::vector<BootRecord>& boot_records);

  // Check if there are too many reboot. If it exceeds the threshold, we skip
  // the reboot action.
  bool IsNormalRebootTooManyTimes();

  // Check if there are too many reboot. If it exceeds the threshold, we skip
  // the reboot action.
  bool IsForceRebootTooManyTimes();

 private:
  // Returns the normal reboot count after a certain time.
  int GetNormalRebootCount(const base::Time& time);

  // Returns the force reboot count after a certain time.
  int GetForceRebootCount(const base::Time& time);

 private:
  // Unowned pointer. Should outlive this instance.
  // Used to communicate with other D-Bus services.
  DbusConnector* const dbus_connector_;
  // Allows to normal reboot or not.
  bool allow_normal_reboot_ = false;
  // Allows to force reboot or not.
  bool allow_force_reboot_ = false;
  // /proc/sysrq-trigger fd.
  int sysrq_fd_ = -1;
  // Cache the BootRecord result to prevent unlimited reboot action.
  std::vector<BootRecord> boot_records_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_ACTION_RUNNER_H_
