// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_H_

#include <vector>

#include <base/time/tick_clock.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/dbus_bindings/bluetooth/dbus-proxies.h"

namespace diagnostics {

// This class abstracts common interfaces for all Bluetooth related routines.
class BluetoothRoutineBase {
 public:
  explicit BluetoothRoutineBase(Context* context);
  BluetoothRoutineBase(const BluetoothRoutineBase&) = delete;
  BluetoothRoutineBase& operator=(const BluetoothRoutineBase&) = delete;
  ~BluetoothRoutineBase();

  // Getter of the main Bluetooth adapter.
  org::bluez::Adapter1ProxyInterface* GetAdapter() const;

  // Ensure the adapter is powered on.
  void EnsureAdapterPoweredOn(base::OnceCallback<void(bool)> on_finish);

 protected:
  // Unowned pointer that should outlive this instance.
  Context* const context_;
  // Routine start time, used to calculate the progress percentage and timeout.
  base::TimeTicks start_ticks_;

 private:
  // The adapters from Bluetooth proxy.
  std::vector<org::bluez::Adapter1ProxyInterface*> adapters_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_BASE_H_
