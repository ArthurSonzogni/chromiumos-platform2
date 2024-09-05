// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_DELEGATE_H_
#define MODEMFWD_DAEMON_DELEGATE_H_

#include <string>

#include <base/functional/callback.h>
#include <brillo/errors/error.h>

namespace modemfwd {

class Modem;
class Task;

class Delegate {
 public:
  virtual ~Delegate() = default;

  virtual void TaskUpdated(Task* task) = 0;
  virtual void FinishTask(Task* task, brillo::ErrorPtr error) = 0;

  virtual void ForceFlashForTesting(
      const std::string& device_id,
      const std::string& carrier_uuid,
      const std::string& variant,
      bool use_modems_fw_info,
      base::OnceCallback<void(const brillo::ErrorPtr&)> callback) = 0;

  virtual bool ResetModem(const std::string& device_id) = 0;

  virtual void RegisterOnStartFlashingCallback(const std::string& equipment_id,
                                               base::OnceClosure callback) = 0;
  virtual void RegisterOnModemReappearanceCallback(
      const std::string& equipment_id, base::OnceClosure callback) = 0;

  virtual void RegisterOnModemStateChangedCallback(
      const std::string& device_id, base::RepeatingClosure callback) = 0;
  virtual void RegisterOnModemPowerStateChangedCallback(
      const std::string& device_id, base::RepeatingClosure callback) = 0;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_DELEGATE_H_
