// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_SYSTEM_STATE_H_
#define UPDATE_ENGINE_COMMON_SYSTEM_STATE_H_

#include <memory>

#include <base/logging.h>

#include "update_engine/common/clock_interface.h"
#include "update_engine/common/prefs_interface.h"

namespace chromeos_update_manager {

class UpdateManager;

}  // namespace chromeos_update_manager

namespace policy {

class DevicePolicy;

}  // namespace policy

namespace chromeos_update_engine {

// SystemState is the root class within the update engine. So we should avoid
// any circular references in header file inclusion. Hence forward-declaring
// the required classes.
class BootControlInterface;
class ConnectionManagerInterface;
class CrosHealthdInterface;
class DlcServiceInterface;
class DlcUtilsInterface;
class HardwareInterface;
class MetricsReporterInterface;
class OmahaRequestParams;
class P2PManager;
class PayloadStateInterface;
class PowerManagerInterface;
class UpdateAttempter;
class CallWrapperInterface;

// An interface to global system context, including platform resources,
// the current state of the system, high-level objects whose lifetime is same
// as main, system interfaces, etc.
// Carved out separately so it can be mocked for unit tests.
class SystemState {
 public:
  virtual ~SystemState() = default;

  static SystemState* Get() {
    CHECK(g_pointer_ != nullptr);
    return g_pointer_;
  }

  // Sets or gets the latest device policy.
  virtual void set_device_policy(const policy::DevicePolicy* device_policy) = 0;
  virtual const policy::DevicePolicy* device_policy() = 0;

  // Gets the interface object for the bootloader control interface.
  virtual BootControlInterface* boot_control() = 0;

  // Gets the interface object for the clock.
  virtual ClockInterface* clock() = 0;

  // Gets the connection manager object.
  virtual ConnectionManagerInterface* connection_manager() = 0;

  // Gets the hardware interface object.
  virtual HardwareInterface* hardware() = 0;

  // Gets the Metrics Library interface for reporting UMA stats.
  virtual MetricsReporterInterface* metrics_reporter() = 0;

  // Gets the interface object for persisted store.
  virtual PrefsInterface* prefs() = 0;

  // Gets the interface object for the persisted store that persists across
  // powerwashes. Please note that this should be used very seldomly and must
  // be forwards and backwards compatible as powerwash is used to go back and
  // forth in system versions.
  virtual PrefsInterface* powerwash_safe_prefs() = 0;

  // Gets the interface for the payload state object.
  virtual PayloadStateInterface* payload_state() = 0;

  // Returns a pointer to the update attempter object.
  virtual UpdateAttempter* update_attempter() = 0;

  // Returns a pointer to the object that stores the parameters that are
  // common to all Omaha requests.
  virtual OmahaRequestParams* request_params() = 0;

  // Returns a pointer to the P2PManager singleton.
  virtual P2PManager* p2p_manager() = 0;

  // Returns a pointer to the UpdateManager singleton.
  virtual chromeos_update_manager::UpdateManager* update_manager() = 0;

  // Gets the power manager object. Mocked during test.
  virtual PowerManagerInterface* power_manager() = 0;

  // If true, this is the first instance of the update engine since the system
  // restarted. Important for tracking whether you are running instance of the
  // update engine on first boot or due to a crash/restart.
  virtual bool system_rebooted() = 0;

  // Returns a pointer to the DlcServiceInterface singleton.
  virtual DlcServiceInterface* dlcservice() = 0;

  // Returns a pointer to the DlcUtilsInterface singleton.
  virtual DlcUtilsInterface* dlc_utils() = 0;

  // Returns a pointer to the CrosHealthdInteraface singleton.
  virtual CrosHealthdInterface* cros_healthd() = 0;

  // Returns a pointer to the CallWrapperInterface singleton.
  virtual CallWrapperInterface* call_wrapper() = 0;

 protected:
  static SystemState* g_pointer_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_SYSTEM_STATE_H_
