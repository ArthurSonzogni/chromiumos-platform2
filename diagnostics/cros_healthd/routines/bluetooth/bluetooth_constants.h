// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_

namespace diagnostics {

// Common status message of all Bluetooth Routines.
inline constexpr char kBluetoothRoutineRunningMessage[] =
    "Bluetooth routine running.";
inline constexpr char kBluetoothRoutinePassedMessage[] =
    "Bluetooth routine passed.";

// Common failure message of all Bluetooth Routines.
inline constexpr char kBluetoothRoutineFailedDiscoveryMode[] =
    "Bluetooth routine is not supported when adapter is in discovery mode.";
inline constexpr char kBluetoothRoutineFailedChangePowered[] =
    "Bluetooth routine failed to change adapter powered status.";

// Common error message of all Bluetooth Routines.
inline constexpr char kBluetoothRoutineFailedGetAdapter[] =
    "Bluetooth routine failed to get main adapter.";
inline constexpr char kBluetoothRoutineUnexpectedFlow[] =
    "Unexpected Bluetooth diagnostic flow.";

// Failure message of Bluetooth power Routines.
inline constexpr char kBluetoothRoutineFailedVerifyPowered[] =
    "Bluetooth routine failed to verify adapter powered status.";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_BLUETOOTH_CONSTANTS_H_
