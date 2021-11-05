// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_METRICS_CONSTANTS_H_
#define RMAD_METRICS_METRICS_CONSTANTS_H_

#include <array>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

// JsonStore additional keys for metrics usage.
inline constexpr char kFirstSetupTimestamp[] = "first_setup_timestamp";
inline constexpr char kSetupTimestamp[] = "setup_timestamp";
inline constexpr char kRunningTime[] = "running_time";
inline constexpr char kRoFirmwareVerified[] = "ro_firmware_verified";
inline constexpr char kWriteProtectDisableMethod[] =
    "write_protect_disable_method";
inline constexpr char kOccurredErrors[] = "occurred_errors";
inline constexpr char kAdditionalActivities[] = "additional_activities";

// Defined RO verification status.
enum class RoVerification : int {
  UNKNOWN = 0,
  PASS = 1,
  UNSUPPORTED = 2,
};

// Defined returning owner.
enum class ReturningOwner : int {
  UNKNOWN = 0,
  SAME_OWNER = 1,
  DIFFERENT_OWNER = 2,
};

// Defined mainboard replacement status.
enum class MainboardReplacement : int {
  UNKNOWN = 0,
  REPLACED = 1,
  ORIGINAL = 2,
};

// Defined write protection disable method.
enum class WriteProtectDisableMethod : int {
  UNKNOWN = 0,
  SKIPPED = 1,
  RSU = 2,
  PHYSICAL_ASSEMBLE_DEVICE = 3,
  PHYSICAL_KEEP_DEVICE_OPEN = 4,
};

constexpr std::array<WriteProtectDisableMethod, 4> kValidWpDisableMethods = {
    WriteProtectDisableMethod::SKIPPED, WriteProtectDisableMethod::RSU,
    WriteProtectDisableMethod::PHYSICAL_ASSEMBLE_DEVICE,
    WriteProtectDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN};

// Defined additional activities.
enum class AdditionalActivity : int {
  NOTHING = 0,
  SHUTDOWN = 1,
  REBOOT = 2,
  BATTERY_CUTOFF = 3,
  DIAGNOSTICS = 4,
};

constexpr std::array<AdditionalActivity, 4> kValidAdditionalActivities = {
    AdditionalActivity::SHUTDOWN, AdditionalActivity::REBOOT,
    AdditionalActivity::BATTERY_CUTOFF, AdditionalActivity::DIAGNOSTICS};

constexpr std::array<AdditionalActivity, 3> kExpectedPowerCycleActivities = {
    AdditionalActivity::SHUTDOWN, AdditionalActivity::REBOOT,
    AdditionalActivity::BATTERY_CUTOFF};

constexpr std::array<RmadErrorCode, 6> kExpectedErrorCodes = {
    RMAD_ERROR_NOT_SET,
    RMAD_ERROR_OK,
    RMAD_ERROR_WAIT,
    RMAD_ERROR_EXPECT_REBOOT,
    RMAD_ERROR_EXPECT_SHUTDOWN,
    RMAD_ERROR_RMA_NOT_REQUIRED};

}  // namespace rmad

#endif  // RMAD_METRICS_METRICS_CONSTANTS_H_
