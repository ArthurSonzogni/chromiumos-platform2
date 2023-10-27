// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_

#include <base/time/time.h>

namespace diagnostics {

// Put the common and important message here to make it clear for our clients.

// Common status message of all Bluetooth routines.
inline constexpr char kBluetoothRoutineRunningMessage[] =
    "Bluetooth routine running.";
inline constexpr char kBluetoothRoutinePassedMessage[] =
    "Bluetooth routine passed.";

// Common failure message of all Bluetooth routines.
inline constexpr char kBluetoothRoutineFailedDiscoveryMode[] =
    "Bluetooth routine is not supported when adapter is in discovery mode.";

// Common error message of all Bluetooth routines.
inline constexpr char kBluetoothRoutineFailedGetAdapter[] =
    "Bluetooth routine failed to get main adapter.";
inline constexpr char kBluetoothRoutineFailedChangePowered[] =
    "Bluetooth routine failed to change adapter powered status.";
inline constexpr char kBluetoothRoutineFailedSwitchDiscovery[] =
    "Bluetooth routine failed to switch adapter discovery mode.";
inline constexpr char kBluetoothRoutineUnexpectedFlow[] =
    "Unexpected Bluetooth diagnostic flow.";

// Failure message of Bluetooth power routine.
inline constexpr char kBluetoothRoutineFailedValidatePowered[] =
    "Bluetooth routine failed to validate adapter powered status.";

// Failure message of Bluetooth discovery routine.
inline constexpr char kBluetoothRoutineFailedValidateDiscovering[] =
    "Bluetooth routine failed to validate adapter discovering status.";

// Failure message of Bluetooth pairing routine.
inline constexpr char kBluetoothRoutineFailedFindTargetPeripheral[] =
    "Bluetooth routine failed to find the device with peripheral ID.";
inline constexpr char kBluetoothRoutineFailedCreateBasebandConnection[] =
    "Bluetooth routine failed to create baseband connection.";
inline constexpr char kBluetoothRoutineFailedFinishPairing[] =
    "Bluetooth routine failed to finish pairing.";

// Bluetooth power routine timeout.
constexpr base::TimeDelta kPowerRoutineTimeout = base::Seconds(15);

// Bluetooth discovery routine timeout.
constexpr base::TimeDelta kDiscoveryRoutineTimeout = base::Seconds(20);

// Bluetooth scanning routine default execution time.
constexpr base::TimeDelta kScanningRoutineDefaultRuntime = base::Seconds(5);

// Bluetooth scanning routine timeout.
constexpr base::TimeDelta kScanningRoutineTimeout = base::Seconds(5);

// We only collect the nearby peripherals information for Bluetooth pairing
// routine. -60 is the recommended threshold for peripherals at 3ft (0.9m)
// distance over air.
constexpr int16_t kNearbyPeripheralMinimumAverageRssi = -60;

// Bluetooth pairing routine timeout.
constexpr base::TimeDelta kPairingRoutineTimeout = base::Seconds(30);

// The identifier for test peripheral in Bluetooth pairing routine.
constexpr char kHealthdBluetoothDiagnosticsTag[] = "<healthd_bt_diag_tag>";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_
