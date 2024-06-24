// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_MANAGER_H_
#define FBPREPROCESSOR_MANAGER_H_

#include <memory>

#include <base/task/sequenced_task_runner.h>
#include <dbus/bus.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/metrics.h"
#include "fbpreprocessor/platform_features_client.h"

namespace fbpreprocessor {

class InputManager;
class OutputManager;
class PseudonymizationManager;
class SessionStateManagerInterface;

class Manager {
 public:
  Manager() = default;
  virtual ~Manager() {}

  // After this function has returned the manager is fully initialized (D-Bus is
  // up, etc) and all the child components are ready for use.
  virtual void Start(dbus::Bus* bus) = 0;

  // Is the user allowed to add firmware dumps to feedback reports? This will
  // return false if any condition (Finch, policy, allowlist, etc.) is not met.
  virtual bool FirmwareDumpsAllowed(FirmwareDump::Type type) const = 0;

  virtual SessionStateManagerInterface* session_state_manager() const = 0;

  virtual PseudonymizationManager* pseudonymization_manager() const = 0;

  virtual OutputManager* output_manager() const = 0;

  virtual InputManager* input_manager() const = 0;

  virtual PlatformFeaturesClient* platform_features() const = 0;

  Metrics& metrics() { return metrics_; }

  virtual scoped_refptr<base::SequencedTaskRunner> task_runner() = 0;

  virtual int default_file_expiration_in_secs() const = 0;

 private:
  Metrics metrics_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_MANAGER_H_
