// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_REAL_SYSTEM_STATE_H_
#define UPDATE_ENGINE_REAL_SYSTEM_STATE_H_

#include "update_engine/system_state.h"

#include <memory>

#include <metrics/metrics_library.h>
#include <policy/device_policy.h>

#include "update_engine/clock.h"
#include "update_engine/connection_manager.h"
#include "update_engine/hardware.h"
#include "update_engine/p2p_manager.h"
#include "update_engine/payload_state.h"
#include "update_engine/prefs.h"
#include "update_engine/real_dbus_wrapper.h"
#include "update_engine/update_attempter.h"
#include "update_engine/update_manager/update_manager.h"

namespace chromeos_update_engine {

// A real implementation of the SystemStateInterface which is
// used by the actual product code.
class RealSystemState : public SystemState {
 public:
  // Constructs all system objects that do not require separate initialization;
  // see Initialize() below for the remaining ones.
  RealSystemState();

  // Initializes and sets systems objects that require an initialization
  // separately from construction. Returns |true| on success.
  bool Initialize();

  inline void set_device_policy(
      const policy::DevicePolicy* device_policy) override {
    device_policy_ = device_policy;
  }

  inline const policy::DevicePolicy* device_policy() override {
    return device_policy_;
  }

  inline ClockInterface* clock() override { return &clock_; }

  inline ConnectionManagerInterface* connection_manager() override {
    return &connection_manager_;
  }

  inline HardwareInterface* hardware() override { return &hardware_; }

  inline MetricsLibraryInterface* metrics_lib() override {
    return &metrics_lib_;
  }

  inline PrefsInterface* prefs() override { return &prefs_; }

  inline PrefsInterface* powerwash_safe_prefs() override {
      return &powerwash_safe_prefs_;
    }

  inline PayloadStateInterface* payload_state() override {
    return &payload_state_;
  }

  inline UpdateAttempter* update_attempter() override {
    return &update_attempter_;
  }

  inline OmahaRequestParams* request_params() override {
    return &request_params_;
  }

  inline P2PManager* p2p_manager() override { return p2p_manager_.get(); }

  inline chromeos_update_manager::UpdateManager* update_manager() override {
    return update_manager_.get();
  }

  inline bool system_rebooted() override { return system_rebooted_; }

 private:
  // Interface for the clock.
  Clock clock_;

  // The latest device policy object from the policy provider.
  const policy::DevicePolicy* device_policy_;

  // The connection manager object that makes download
  // decisions depending on the current type of connection.
  ConnectionManager connection_manager_;

  // Interface for the hardware functions.
  Hardware hardware_;

  // The Metrics Library interface for reporting UMA stats.
  MetricsLibrary metrics_lib_;

  // Interface for persisted store.
  Prefs prefs_;

  // Interface for persisted store that persists across powerwashes.
  Prefs powerwash_safe_prefs_;

  // All state pertaining to payload state such as
  // response, URL, backoff states.
  PayloadState payload_state_;

  // The dbus object used to initialize the update attempter.
  RealDBusWrapper dbus_;

  // Pointer to the update attempter object.
  UpdateAttempter update_attempter_;

  // Common parameters for all Omaha requests.
  OmahaRequestParams request_params_;

  std::unique_ptr<P2PManager> p2p_manager_;

  std::unique_ptr<chromeos_update_manager::UpdateManager> update_manager_;

  policy::PolicyProvider policy_provider_;

  // If true, this is the first instance of the update engine since the system
  // rebooted. Important for tracking whether you are running instance of the
  // update engine on first boot or due to a crash/restart.
  bool system_rebooted_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_REAL_SYSTEM_STATE_H_
