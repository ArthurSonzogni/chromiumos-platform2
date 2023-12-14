// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_ACTION_RUNNER_H_
#define HEARTD_DAEMON_ACTION_RUNNER_H_

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

class ActionRunner {
 public:
  ActionRunner();
  ActionRunner(const ActionRunner&) = delete;
  ActionRunner& operator=(const ActionRunner&) = delete;
  ~ActionRunner();

  // Run the |action| for service |name|.
  void Run(ash::heartd::mojom::ServiceName name,
           ash::heartd::mojom::ActionType action);

  // Enables the normal reboot action.
  void EnableNormalRebootAction();

  // Enables the force reboot action.
  void EnableForceRebootAction();

  // Disable the normal reboot action.
  void DisableNormalRebootAction();

  // Disable the force reboot action.
  void DisableForceRebootAction();

 private:
  // Allows to normal reboot or not.
  bool allow_normal_reboot_ = false;
  // Allows to force reboot or not.
  bool allow_force_reboot_ = false;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_ACTION_RUNNER_H_
