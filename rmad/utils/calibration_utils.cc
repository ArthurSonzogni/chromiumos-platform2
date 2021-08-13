// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/calibration_utils.h"

#include <string>

#include <base/logging.h>

namespace rmad {

bool IsValidCalibrationComponent(RmadComponent component) {
  for (auto component_instruction : kCalibrationSetupInstruction) {
    if (component == component_instruction.first) {
      return true;
    }
  }
  return false;
}

CalibrationSetupInstruction GetCalibrationSetupInstruction(
    RmadComponent component) {
  CalibrationSetupInstruction setup_instruction =
      RMAD_CALIBRATION_INSTRUCTION_UNKNOWN;
  for (auto calibration_priority : kCalibrationSetupInstruction) {
    if (calibration_priority.first == component) {
      setup_instruction = calibration_priority.second;
      break;
    }
  }

  if (setup_instruction == RMAD_CALIBRATION_INSTRUCTION_UNKNOWN) {
    LOG(ERROR) << "Unknown setup instruction for the device "
               << RmadComponent_Name(component);
  }

  return setup_instruction;
}

bool ShouldCalibrate(CalibrationComponentStatus::CalibrationStatus status) {
  return status == CalibrationComponentStatus::RMAD_CALIBRATION_WAITING ||
         status == CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS ||
         status == CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
}

bool ShouldCalibrateComponent(CalibrationComponentStatus component_status) {
  if (!IsValidCalibrationComponent(component_status.component())) {
    LOG(WARNING) << RmadComponent_Name(component_status.component())
                 << " is invalid for calibration.";
    return false;
  }
  if (component_status.status() ==
      CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN) {
    LOG(ERROR) << "Rmad: Calibration status for "
               << component_status.component() << " is missing.";
    return false;
  }

  return ShouldCalibrate(component_status.status());
}

bool GetCalibrationMap(scoped_refptr<JsonStore> json_store,
                       InstructionCalibrationStatusMap* calibration_map) {
  if (!calibration_map) {
    LOG(ERROR) << "Missing output field of the calibration map";
    return false;
  }

  std::map<std::string, std::map<std::string, std::string>> json_value_map;
  if (!json_store->GetValue(kCalibrationMap, &json_value_map)) {
    LOG(ERROR) << "Cannot get variables from the json store";
    return false;
  }

  for (auto instruction_components : json_value_map) {
    CalibrationSetupInstruction setup_instruction;
    if (!CalibrationSetupInstruction_Parse(instruction_components.first,
                                           &setup_instruction)) {
      LOG(ERROR) << "Failed to parse setup instruction from variables";
      return false;
    }

    for (auto component_status : instruction_components.second) {
      RmadComponent component;
      if (!RmadComponent_Parse(component_status.first, &component)) {
        LOG(ERROR) << "Failed to parse component name from variables";
        return false;
      }
      CalibrationComponentStatus::CalibrationStatus status;
      if (!CalibrationComponentStatus::CalibrationStatus_Parse(
              component_status.second, &status)) {
        LOG(ERROR) << "Failed to parse status name from variables";
        return false;
      }
      (*calibration_map)[setup_instruction][component] = status;
      if (component == RmadComponent::RMAD_COMPONENT_UNKNOWN) {
        LOG(ERROR) << "Rmad: Calibration component is missing.";
        return false;
      }
      if (status == CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN) {
        LOG(ERROR) << "Rmad: Calibration status for " << component_status.first
                   << " is missing.";
        return false;
      }
      if (!IsValidCalibrationComponent(component)) {
        LOG(ERROR) << "Rmad: " << component_status.first
                   << " cannot be calibrated.";
        return false;
      }
    }
  }

  return true;
}

bool SetCalibrationMap(scoped_refptr<JsonStore> json_store,
                       const InstructionCalibrationStatusMap& calibration_map) {
  // In order to save dictionary style variables to json, currently only
  // variables whose keys are strings are supported. This is why we converted
  // it to a string. In addition, in order to ensure that the file is still
  // readable after the enum sequence is updated, we also convert its value
  // into a readable string to deal with possible updates.
  std::map<std::string, std::map<std::string, std::string>> json_value_map;
  for (auto setup_instruction_components : calibration_map) {
    std::string instruction =
        CalibrationSetupInstruction_Name(setup_instruction_components.first);
    for (auto component_status : setup_instruction_components.second) {
      std::string component_name = RmadComponent_Name(component_status.first);
      std::string status_name =
          CalibrationComponentStatus::CalibrationStatus_Name(
              component_status.second);
      json_value_map[instruction][component_name] = status_name;
    }
  }

  return json_store->SetValue(kCalibrationMap, json_value_map);
}

bool GetCurrentSetupInstruction(
    const InstructionCalibrationStatusMap& calibration_map,
    CalibrationSetupInstruction* setup_instruction) {
  if (!setup_instruction) {
    LOG(ERROR) << "output field is not set";
    return false;
  }

  *setup_instruction = RMAD_CALIBRATION_INSTRUCTION_NO_NEED_CALIBRATION;

  for (auto instruction_components : calibration_map) {
    for (auto component_status : instruction_components.second) {
      if (ShouldCalibrate(component_status.second) &&
          *setup_instruction >= instruction_components.first) {
        *setup_instruction = instruction_components.first;
      }
    }
  }

  return true;
}

}  // namespace rmad
