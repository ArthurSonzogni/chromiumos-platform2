// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_H_
#define POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_H_

#include <string>

#include <base/files/file_path.h>

namespace power_manager {

class PrefsInterface;

namespace system {

// Abstraction layer that allows us to mock MachineQuirks when testing.
class MachineQuirksInterface {
 public:
  virtual ~MachineQuirksInterface() = default;
  virtual void Init(PrefsInterface* prefs) = 0;
  // When a machine quirk is found, set the corresponding pref to 1
  virtual void ApplyQuirksToPrefs() = 0;
  // Checks if the machine quirk indicates that
  // the suspend should be blocked.
  virtual bool IsSuspendBlocked() = 0;
  // Checks if the machine quirk indicates that
  // the suspend should be allowed but only to
  // Idle (freeze).
  virtual bool IsSuspendToIdle() = 0;
  // Checks if the machine quirk indicates that
  // the device doesn't have an internal monitor.
  virtual bool IsExternalDisplayOnly() = 0;
  // Checks if the machine quirk indicates that
  // the device uses the generic ACPI battery.
  virtual bool IsGenericAcpiBatteryDriver() = 0;
};

// Check for machine specific quirks from the running machine.
// When broken devices are discovered in testing, they get added
// to lists in the /usr/share/power_manager directory on the device.
// This class uses those lists to make decisions.
// Some machines and configurations have broken behavior
// and certain power_manager actions must be avoided.
class MachineQuirks : public MachineQuirksInterface {
 public:
  MachineQuirks();
  MachineQuirks(const MachineQuirks&) = delete;
  MachineQuirks& operator=(const MachineQuirks&) = delete;

  ~MachineQuirks() override = default;

  void Init(PrefsInterface* prefs) override;
  // When a machine quirk is found, set the corresponding pref to 1
  void ApplyQuirksToPrefs() override;

  // Determine if the machine is blocked from suspending.
  // These workarounds are required due to certain models
  // being unable to suspend and resume properly.
  bool IsSuspendBlocked() override;

  // Determine if the machine should use suspend-to-idle
  // instead of suspending.
  // This quirk is for machines which do not return from a suspend-to-ram
  // case. They do work if the system is suspend-to-idle. (freeze)
  bool IsSuspendToIdle() override;

  // Determine if the machine does not have an internal monitor.
  // TODO(b/325660762): The current implementation uses regex to match the model
  // name now. Re-implement this function to determine at run time if the
  // machine has an internal monitor or not.
  bool IsExternalDisplayOnly() override;

  // Determine if the machine is using the generic ACPI battery driver, that is,
  // linux/drivers/acpi/battery.c
  // This quirk is required as the generic ACPI battery driver can read out the
  // current charge as 0. Such devices then cause various power related tools to
  // crash as they do not expect to receive a 0 value for current charge.
  bool IsGenericAcpiBatteryDriver() override;

  // Reads the DMI value, given a DMI filename
  bool ReadDMIValFromFile(std::string_view dmi_file_name,
                          std::string* value_out);

  // Return true if DMI val in the pref string is equal to the product name of
  // the device
  bool IsProductNameMatch(std::string_view product_name_pref);

  // Return true if DMI val(s) in string match DMI val(s) of device
  bool IsDMIMatch(std::string_view dmi_entry);

  // Return true if the DMI IDs pref contains a DMI entry that matches the
  // device's DMI val(s).
  bool ContainsDMIMatch(std::string_view dmi_ids_pref);

  // Functions used to pass in mock directories for unit tests
  void set_dmi_id_dir_for_test(const base::FilePath& dir) { dmi_id_dir_ = dir; }
  void set_power_supply_dir_for_test(const base::FilePath& dir) {
    power_supply_dir_ = dir;
  }

 private:
  base::FilePath dmi_id_dir_;
  base::FilePath power_supply_dir_;

  PrefsInterface* prefs_ = nullptr;  // non-owned
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_MACHINE_QUIRKS_H_
