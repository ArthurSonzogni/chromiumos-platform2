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

// Files for pre-stop script to read.
inline constexpr char kCutoffRequestFilePath[] = ".battery_cutoff_request";

// JsonStore rmad_interface keys.
// Update go/shimless-state-preservation when adding new fields.
inline constexpr char kStateHistory[] = "state_history";
inline constexpr char kStateMap[] = "state_map";
inline constexpr char kNetworkConnected[] = "network_connected";
inline constexpr char kReplacedComponentNames[] = "replaced_component_names";
inline constexpr char kSameOwner[] = "same_owner";
inline constexpr char kWpDisableSkipped[] = "wp_disable_skipped";
inline constexpr char kMlbRepair[] = "mlb_repair";
inline constexpr char kCalibrationMap[] = "calibration_map";

// Component traits.
inline constexpr std::array<RmadComponent, 3> kComponentsNeedManualCalibration =
    {RMAD_COMPONENT_GYROSCOPE, RMAD_COMPONENT_BASE_ACCELEROMETER,
     RMAD_COMPONENT_LID_ACCELEROMETER};
inline constexpr std::array<RmadComponent, 2> kComponentsNeedAutoCalibration = {
    RMAD_COMPONENT_AUDIO_CODEC,
    RMAD_COMPONENT_TOUCHSCREEN,
};

}  // namespace rmad

#endif  // RMAD_CONSTANTS_H_
