// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CALIBRATION_UTILS_H_
#define RMAD_UTILS_CALIBRATION_UTILS_H_

#include <array>
#include <map>
#include <utility>

#include <base/memory/scoped_refptr.h>

#include "rmad/constants.h"
#include "rmad/utils/json_store.h"

namespace rmad {

inline constexpr std::
    array<std::pair<RmadComponent, CalibrationSetupInstruction>, 3>
        kCalibrationSetupInstruction = {
            {{RmadComponent::RMAD_COMPONENT_GYROSCOPE,
              RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE},
             {RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER,
              RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE},
             {RmadComponent::RMAD_COMPONENT_LID_ACCELEROMETER,
              RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE}}};

// Check if the component can be calibrated.
bool IsValidCalibrationComponent(RmadComponent component);

// Get the setup instruction for calibration according to the given component.
CalibrationSetupInstruction GetCalibrationSetupInstruction(
    RmadComponent component);

// Check whether calibration is required according to the calibration status.
bool ShouldCalibrate(CalibrationComponentStatus::CalibrationStatus status);

// Check if a component with specific calibration status can be calibrated.
bool ShouldCalibrateComponent(CalibrationComponentStatus component_status);

using InstructionCalibrationStatusMap = std::map<
    CalibrationSetupInstruction,
    std::map<RmadComponent, CalibrationComponentStatus::CalibrationStatus>>;

// Get the current calibration status and setup instructions of each sensor from
// the given json storage.
bool GetCalibrationMap(scoped_refptr<JsonStore> json_store,
                       InstructionCalibrationStatusMap* calibration_map);

// Set the current calibration status and setup instructions of each sensor to
// the given json storage.
bool SetCalibrationMap(scoped_refptr<JsonStore> json_store,
                       const InstructionCalibrationStatusMap& calibration_map);

// Get current setup instructions by providing calibration status of all
// sensors.
bool GetCurrentSetupInstruction(
    const InstructionCalibrationStatusMap& calibration_map,
    CalibrationSetupInstruction* setup_instruction);

}  // namespace rmad

#endif  // RMAD_UTILS_CALIBRATION_UTILS_H_
