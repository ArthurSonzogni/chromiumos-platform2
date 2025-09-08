// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_CONSTANTS_H_
#define RMAD_CONSTANTS_H_

#include <string_view>

#include <base/containers/fixed_flat_map.h>
#include <base/containers/fixed_flat_set.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

// Mode flags to control Shimless RMA's behaviors
enum : uint64_t {
  kShimlessModeFlagsTriggerable = 1 << 0,               // 0x1
  kShimlessModeFlagsBoardIdCheckResultBypass = 1 << 1,  // 0x2
  kShimlessModeFlagsPreserveGbbFlags = 1 << 2,          // 0x4
  kShimlessModeFlagsRaccResultBypass = 1 << 3,          // 0x8
};

// Pipe name for internal mojo connection between D-Bus daemon and executor.
inline constexpr char kRmadInternalMojoPipeName[] = "rmad_internal";

inline constexpr char kDefaultWorkingDirPath[] = "/var/lib/rmad/";
inline constexpr char kDefaultConfigDirPath[] = "/etc/rmad";
inline constexpr char kDefaultRmadConfigProtoFilePath[] =
    "rmad_config.textproto";
inline constexpr char kDefaultUnencryptedPreserveFilePath[] =
    "/mnt/stateful_partition/unencrypted/preserve";

inline constexpr char kPreserveRmaStateHelperPath[] =
    "/usr/bin/preserve_rma_state";

// Files in unencrypted RMA directory.
inline constexpr char kDefaultUnencryptedRmaDirPath[] =
    "/mnt/stateful_partition/unencrypted/rma-data";
inline constexpr char kJsonStoreFilePath[] = "state";

// Files for testing purpose.
inline constexpr char kTestDirPath[] = ".test";
inline constexpr char kDisablePowerwashFilePath[] = ".disable_powerwash";
inline constexpr char kDisableCalibrationFilePath[] = ".disable_calibration";
inline constexpr char kDisableRaccFilePath[] = ".disable_racc";
inline constexpr char kFakeFeaturesInputFilePath[] = ".fake_features_input";
inline constexpr char kFakeFeaturesOutputFilePath[] = ".fake_features_output";

// We currently treat InitialState as WelcomeState.
inline constexpr RmadState::StateCase kInitialStateCase = RmadState::kWelcome;

// JsonStore rmad_interface keys.
// Update go/shimless-state-preservation when adding new fields.
inline constexpr char kStateHistory[] = "state_history";
inline constexpr char kStateMap[] = "state_map";
inline constexpr char kNetworkConnected[] = "network_connected";
inline constexpr char kReplacedComponentNames[] = "replaced_component_names";
inline constexpr char kSameOwner[] = "same_owner";
inline constexpr char kWpDisableRequired[] = "wp_disable_required";
inline constexpr char kCcdBlocked[] = "ccd_blocked";
inline constexpr char kWipeDevice[] = "wipe_device";
inline constexpr char kWpDisableMethod[] = "wp_disable_method";
inline constexpr char kMlbRepair[] = "mlb_repair";
inline constexpr char kGscRebooted[] = "gsc_rebooted";
inline constexpr char kFirmwareUpdated[] = "firmware_updated";
inline constexpr char kCalibrationMap[] = "calibration_map";
inline constexpr char kCalibrationInstruction[] = "calibration_instruction";
inline constexpr char kProvisionFinishedStatus[] = "provision_finished_status";
inline constexpr char kPowerwashCount[] = "powerwash_count";
inline constexpr char kRoFirmwareVerified[] = "ro_firmware_verified";
inline constexpr char kFinalizeRebooted[] = "finalize_rebooted";
// Indicates that Shimless RMA is initiated by the spare MLB flow in factory.
inline constexpr char kSpareMlb[] = "spare_mlb";
// Shimless RMA mode for different testing scenarios.
inline constexpr char kShimlessMode[] = "shimless_mode";

// Component traits.
inline constexpr auto kComponentsNeedManualCalibration =
    base::MakeFixedFlatSet<RmadComponent>(
        {RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
         RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});
inline constexpr auto kComponentsNeedUpdateCbi =
    base::MakeFixedFlatSet<RmadComponent>(
        {RMAD_COMPONENT_BASE_GYROSCOPE, RMAD_COMPONENT_LID_GYROSCOPE});

// We map RmadState::StateCase (enum) to std::string_view to represent state in
// a more readable way.
inline constexpr auto kStateNames =
    base::MakeFixedFlatMap<RmadState::StateCase, std::string_view>({
        {RmadState::kWelcome, "Welcome"},
        {RmadState::kComponentsRepair, "ComponentsRepair"},
        {RmadState::kDeviceDestination, "DeviceDestination"},
        {RmadState::kWipeSelection, "WipeSelection"},
        {RmadState::kWpDisableMethod, "WpDisableMethod"},
        {RmadState::kWpDisableRsu, "WpDisableRsu"},
        {RmadState::kWpDisablePhysical, "WpDisablePhysical"},
        {RmadState::kWpDisableComplete, "WpDisableComplete"},
        {RmadState::kUpdateRoFirmware, "UpdateRoFirmware"},
        {RmadState::kRestock, "Restock"},
        {RmadState::kUpdateDeviceInfo, "UpdateDeviceInfo"},
        {RmadState::kProvisionDevice, "ProvisionDevice"},
        {RmadState::kSetupCalibration, "SetupCalibration"},
        {RmadState::kRunCalibration, "RunCalibration"},
        {RmadState::kCheckCalibration, "CheckCalibration"},
        {RmadState::kWpEnablePhysical, "WpEnablePhysical"},
        {RmadState::kFinalize, "Finalize"},
        {RmadState::kRepairComplete, "RepairComplete"},
    });

}  // namespace rmad

#endif  // RMAD_CONSTANTS_H_
