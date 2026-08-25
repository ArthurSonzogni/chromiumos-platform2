// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_

#include <optional>
#include <string>

#include "power_manager/powerd/system/thermal/device_thermal_state.h"
#include "power_manager/powerd/system/thermal/thermal_device.h"

namespace power_manager::system {

class CoolingDevice : public ThermalDevice {
 public:
  using ThermalDevice::ThermalDevice;
  CoolingDevice(const CoolingDevice&) = delete;
  CoolingDevice& operator=(const CoolingDevice&) = delete;
  // Read sysfs to determine the scaling for nominal/fair/serious/critical
  // state.
  bool InitSysfsFile() override;

  void set_sys_cpu_dir_for_testing(base::FilePath sys_cpu_dir) {
    sys_cpu_dir_ = sys_cpu_dir;
  }

 protected:
  // ThermalDevice override.
  DeviceThermalState CalculateThermalState(int sysfs_data) override;

 private:
  // Initialize weight for SoC cooling devices using layered discovery.
  void InitSocWeight(const std::string& type_str);

  // Discover cooling device weight from device-tree thermal zones.
  std::optional<double> FindDtsThermalWeight() const;

  // Calculate cooling device weight from EAS CPU capacity and cluster size.
  std::optional<double> CalculateEasCpuWeight(
      const std::string& type_str) const;

  // Root sysfs directory for CPU devices.
  base::FilePath sys_cpu_dir_ = base::FilePath("/sys/devices/system/cpu");

  // Value of max_state in cooling device sysfs.
  int max_state_ = 0;

  // Threshold of cur_state in cooling device sysfs for each DeviceThermalState.
  int threshold_fair_ = 0;
  int threshold_serious_ = 0;
  int threshold_critical_ = 0;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_
