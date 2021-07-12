// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_CONSTANTS_H_
#define RMAD_CONSTANTS_H_

#include <array>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

// JsonStore rmad_interface keys.
inline constexpr char kStateHistory[] = "state_history";
inline constexpr char kStateMap[] = "state_map";
inline constexpr char kNetworkConnected[] = "network_connected";
inline constexpr char kReplacedComponentNames[] = "replaced_component_names";
inline constexpr char kSameOwner[] = "same_owner";
inline constexpr char kCalibrationMap[] = "calibration_map";

// Component traits.
inline constexpr std::array<
    ComponentsRepairState::ComponentRepairStatus::Component,
    3>
    kComponentsNeedManualCalibration = {
        ComponentsRepairState::ComponentRepairStatus::RMAD_COMPONENT_GYROSCOPE,
        ComponentsRepairState::ComponentRepairStatus::
            RMAD_COMPONENT_ACCELEROMETER,
        ComponentsRepairState::ComponentRepairStatus::
            RMAD_COMPONENT_MAINBOARD_REWORK};
inline constexpr std::
    array<ComponentsRepairState::ComponentRepairStatus::Component, 3>
        kComponentsNeedAutoCalibration = {
            ComponentsRepairState::ComponentRepairStatus::
                RMAD_COMPONENT_AUDIO_CODEC,
            ComponentsRepairState::ComponentRepairStatus::
                RMAD_COMPONENT_TOUCHSCREEN,
            ComponentsRepairState::ComponentRepairStatus::
                RMAD_COMPONENT_MAINBOARD_REWORK,
};

}  // namespace rmad

#endif  // RMAD_CONSTANTS_H_
