// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/power_manager_proxy.h"

#include <string>
#include <vector>

#include <power_manager/proto_bindings/suspend.pb.h>

#include "power_manager/proto_bindings/power_supply_properties.pb.h"
#include "swap_management/suspend_history.h"

namespace swap_management {

namespace {

// Handles the result of an attempt to connect to a D-Bus signal.
void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface << "." << signal;
    return;
  }
  VLOG(2) << "Successfully connected to D-Bus signal " << interface << "."
          << signal;
}

void OnSuspendImminent(const std::vector<uint8_t>& data) {
  SuspendHistory::Get()->OnSuspendImminent();
}

void OnSuspendDone(const std::vector<uint8_t>& data) {
  power_manager::SuspendDone proto;
  if (!proto.ParseFromArray(data.data(), data.size())) {
    LOG(ERROR) << "Failed to parse suspend done signal";
  }
  SuspendHistory::Get()->OnSuspendDone(
      base::Microseconds(proto.suspend_duration()));
}
}  // namespace

PowerManagerProxy* PowerManagerProxy::Get() {
  return *GetSingleton<PowerManagerProxy>();
}

PowerManagerProxy::PowerManagerProxy() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> power_management_proxy_bus = new dbus::Bus(options);
  power_manager_proxy_ = std::make_unique<org::chromium::PowerManagerProxy>(
      power_management_proxy_bus);
}

void PowerManagerProxy::RegisterSuspendSignal() {
  [[maybe_unused]] static bool register_once = [&]() {
    power_manager_proxy_->RegisterSuspendImminentSignalHandler(
        base::BindRepeating(&OnSuspendImminent),
        base::BindOnce(&HandleSignalConnected));
    power_manager_proxy_->RegisterSuspendDoneSignalHandler(
        base::BindRepeating(&OnSuspendDone),
        base::BindOnce(&HandleSignalConnected));
    return true;
  }();
}
absl::StatusOr<bool> PowerManagerProxy::IsACConnected() {
  uint32_t power_type, battery_state;
  double display_battery_percentage;
  brillo::ErrorPtr error;
  if (!power_manager_proxy_->GetBatteryState(
          &power_type, &battery_state, &display_battery_percentage, &error)) {
    return absl::UnavailableError("power_manage_proxy: " + error->GetMessage());
  }
  return power_type == power_manager::PowerSupplyProperties_ExternalPower_AC;
}

}  // namespace swap_management
