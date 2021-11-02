// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_CONSTANTS_H_
#define RMAD_CONSTANTS_H_

#include <array>
#include <utility>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

inline constexpr char kDefaultWorkingDirPath[] = "/var/lib/rmad/";
inline constexpr char kDefaultJsonStoreFilePath[] =
    "/mnt/stateful_partition/unencrypted/rma-data/state";
inline constexpr char kTestDirPath[] = ".test";

// Files for pre-stop script to read.
inline constexpr char kPowerwashRequestFilePath[] = ".powerwash_request";
inline constexpr char kCutoffRequestFilePath[] = ".battery_cutoff_request";

// Files for testing purpose.
inline constexpr char kDisablePowerwashFilePath[] = ".disable_powerwash";

// JsonStore rmad_interface keys.
// Update go/shimless-state-preservation when adding new fields.
inline constexpr char kStateHistory[] = "state_history";
inline constexpr char kStateMap[] = "state_map";
inline constexpr char kNetworkConnected[] = "network_connected";
inline constexpr char kReplacedComponentNames[] = "replaced_component_names";
inline constexpr char kSameOwner[] = "same_owner";
inline constexpr char kWpDisableSkipped[] = "wp_disable_skipped";
inline constexpr char kKeepDeviceOpen[] = "keep_device_open";
inline constexpr char kMlbRepair[] = "mlb_repair";
inline constexpr char kCalibrationMap[] = "calibration_map";
inline constexpr char kPowerwashRequest[] = "powerwash_request";

// Component traits.
inline constexpr std::array<RmadComponent, 4> kComponentsNeedManualCalibration =
    {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
     RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE};
inline constexpr std::array<RmadComponent, 2> kComponentsNeedAutoCalibration = {
    RMAD_COMPONENT_AUDIO_CODEC,
    RMAD_COMPONENT_TOUCHSCREEN,
};

// Constants for fake utilities.
namespace fake {

inline constexpr char kRebootRequestFilePath[] = "reboot_request";
inline constexpr char kShutdownRequestFilePath[] = "shutdown_request";

inline constexpr char kRoVerificationStatusFilePath[] =
    "ro_verification_status";
inline constexpr char kHwVerificationResultFilePath[] =
    "hardware_verification_result";
inline constexpr char kHwwpDisabledFilePath[] =
    "hardware_write_protect_disabled";
inline constexpr char kFactoryModeEnabledFilePath[] = "factory_mode_enabled";
inline constexpr char kBlockCcdFilePath[] = "block_ccd";
inline constexpr char kCrosSystemFilePath[] = "crossystem";

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_CONSTANTS_H_
