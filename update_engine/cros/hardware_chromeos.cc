//
// Copyright (C) 2013 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/hardware_chromeos.h"

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_file_value_serializer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/key_value_store.h>
#include <debugd/dbus-constants.h>
#include <libcrossystem/crossystem.h>
#include <vboot/crossystem.h>

extern "C" {
#include "vboot/vboot_host.h"
}

#include "update_engine/common/constants.h"
#include "update_engine/common/hardware.h"
#include "update_engine/common/hwid_override.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/boot_control_chromeos.h"
#include "update_engine/cros/dbus_connection.h"
#if USE_CFM || USE_REPORT_REQUISITION
#include "update_engine/cros/requisition_util.h"
#endif

using std::string;
using std::vector;

namespace {

const char kOOBECompletedMarker[] = "/home/chronos/.oobe_completed";

// The stateful directory used by update_engine to store powerwash-safe files.
// The files stored here must be added to the powerwash script allowlist.
const char kPowerwashSafeDirectory[] =
    "/mnt/stateful_partition/unencrypted/preserve";

// The powerwash_count marker file contains the number of times the device was
// powerwashed. This value is incremented by the clobber-state script when
// a powerwash is performed.
const char kPowerwashCountMarker[] = "powerwash_count";

// The path of the marker file used to trigger powerwash when post-install
// completes successfully so that the device is powerwashed on next reboot.
constexpr char kPowerwashMarkerPath[] =
    "mnt/stateful_partition/factory_install_reset";

// Expected tag in the powerwash marker file that indicates that
// powerwash is initiated by the update engine.
constexpr char kPowerwashReasonUpdateEngineTag[] = "reason=update_engine";

// The name of the marker file used to trigger a save of rollback data
// during the next shutdown.
const char kRollbackSaveMarkerFile[] =
    "/mnt/stateful_partition/.save_rollback_data";

// The contents of the powerwash marker file for the non-rollback case.
const char kPowerwashCommand[] = "safe fast keepimg reason=update_engine\n";

// The contents of the powerwas marker file for the rollback case.
const char kRollbackPowerwashCommand[] =
    "safe fast keepimg rollback reason=update_engine\n";

#if USE_LVM_STATEFUL_PARTITION
// Powerwash marker when preserving logical volumes.
// Append at the front.
const char kPowerwashPreserveLVs[] = "preserve_lvs";
#endif  // USE_LVM_STATEFUL_PARTITION

// UpdateManager config path.
const char* kConfigFilePath = "/etc/update_manager.conf";

// UpdateManager config options:
const char* kConfigOptsIsOOBEEnabled = "is_oobe_enabled";

const char* kActivePingKey = "first_active_omaha_ping_sent";

// The week when the device was first used.
const char* kActivateDateVpdKey = "ActivateDate";

// The FSI version the device shipped with.
const char* kFsiVersionVpdKey = "fsi_version";

// Vboot MiniOS booting priority flag.
const char kMiniOsPriorityFlag[] = "minios_priority";

const char kKernelCmdline[] = "proc/cmdline";

const char kRunningFromMiniOSLabel[] = "cros_minios";

constexpr char kLocalStatePath[] = "/home/chronos/Local State";

constexpr char kEnrollmentRecoveryRequired[] = "EnrollmentRecoveryRequired";

constexpr char kConsumerSegment[] = "IsConsumerSegment";

// Firmware slot to try next (A or B).
constexpr char kFWTryNextFlag[] = "fw_try_next";

// Current main firmware.
constexpr char kMainFWActFlag[] = "mainfw_act";

// Firmware boot result this boot.
constexpr char kFWResultFlag[] = "fw_result";

// Number of times to try to boot `kFWTryNextFlag` slot.
constexpr char kFWTryCountFlag[] = "fw_try_count";

// Firmware partition slots.
constexpr char kFWSlotA[] = "A";
constexpr char kFWSlotB[] = "B";
}  // namespace

namespace chromeos_update_engine {

namespace hardware {

// Factory defined in hardware.h.
std::unique_ptr<HardwareInterface> CreateHardware() {
  std::unique_ptr<HardwareChromeOS> hardware(new HardwareChromeOS());
  hardware->Init();
  return std::move(hardware);
}

}  // namespace hardware

HardwareChromeOS::HardwareChromeOS()
    : root_("/"), non_volatile_path_(constants::kNonVolatileDirectory) {}

void HardwareChromeOS::Init() {
  LoadConfig("" /* root_prefix */, IsNormalBootMode());
  debugd_proxy_.reset(
      new org::chromium::debugdProxy(DBusConnection::Get()->GetDBus()));
  crossystem_.reset(new crossystem::Crossystem());
}

bool HardwareChromeOS::IsOfficialBuild() const {
  return VbGetSystemPropertyInt("debug_build") == 0;
}

bool HardwareChromeOS::IsNormalBootMode() const {
  return VbGetSystemPropertyInt("devsw_boot") == 0;
}

bool HardwareChromeOS::IsRunningFromMiniOs() const {
  // Look up the current kernel command line.
  string kernel_cmd_line;
  if (!base::ReadFileToString(base::FilePath(root_.Append(kKernelCmdline)),
                              &kernel_cmd_line)) {
    LOG(ERROR) << "Can't read kernel commandline options.";
    return false;
  }

  size_t match_start = 0;
  while ((match_start = kernel_cmd_line.find(
              kRunningFromMiniOSLabel, match_start)) != std::string::npos) {
    // Make sure the MiniOS flag is not a part of any other key by checking for
    // a space or a quote after it, except if it is at the end of the string.
    match_start += sizeof(kRunningFromMiniOSLabel) - 1;
    if (match_start < kernel_cmd_line.size() &&
        kernel_cmd_line[match_start] != ' ' &&
        kernel_cmd_line[match_start] != '"') {
      // Ignore partial matches.
      continue;
    }
    return true;
  }
  return false;
}

bool HardwareChromeOS::AreDevFeaturesEnabled() const {
  // Even though the debugd tools are also gated on devmode, checking here can
  // save us a D-Bus call so it's worth doing explicitly.
  if (IsNormalBootMode())
    return false;

  int32_t dev_features = debugd::DEV_FEATURES_DISABLED;
  brillo::ErrorPtr error;
  // Some boards may not include debugd so it's expected that this may fail,
  // in which case we treat it as disabled.
  if (debugd_proxy_ && debugd_proxy_->QueryDevFeatures(&dev_features, &error) &&
      !(dev_features & debugd::DEV_FEATURES_DISABLED)) {
    LOG(INFO) << "Debugd dev tools enabled.";
    return true;
  }
  return false;
}

bool HardwareChromeOS::IsOOBEEnabled() const {
  return is_oobe_enabled_;
}

bool HardwareChromeOS::IsOOBEComplete(base::Time* out_time_of_oobe) const {
  if (!is_oobe_enabled_) {
    LOG(WARNING) << "OOBE is not enabled but IsOOBEComplete() was called";
  }
  struct stat statbuf;
  if (stat(kOOBECompletedMarker, &statbuf) != 0) {
    if (errno != ENOENT) {
      PLOG(ERROR) << "Error getting information about " << kOOBECompletedMarker;
    }
    return false;
  }

  if (out_time_of_oobe != nullptr)
    *out_time_of_oobe = base::Time::FromTimeT(statbuf.st_mtime);
  return true;
}

static string ReadValueFromCrosSystem(const string& key) {
  char value_buffer[VB_MAX_STRING_PROPERTY];

  if (VbGetSystemPropertyString(
          key.c_str(), value_buffer, sizeof(value_buffer)) != -1) {
    string return_value(value_buffer);
    base::TrimWhitespaceASCII(return_value, base::TRIM_ALL, &return_value);
    return return_value;
  }

  LOG(ERROR) << "Unable to read crossystem key " << key;
  return "";
}

string HardwareChromeOS::GetHardwareClass() const {
  if (USE_HWID_OVERRIDE) {
    return HwidOverride::Read(base::FilePath("/"));
  }
  return ReadValueFromCrosSystem("hwid");
}

string HardwareChromeOS::GetDeviceRequisition() const {
#if USE_CFM || USE_REPORT_REQUISITION
  return ReadDeviceRequisition(ReadLocalState().get());
#else
  return "";
#endif
}

int HardwareChromeOS::GetMinKernelKeyVersion() const {
  return VbGetSystemPropertyInt("tpm_kernver");
}

int HardwareChromeOS::GetMaxFirmwareKeyRollforward() const {
  return VbGetSystemPropertyInt("firmware_max_rollforward");
}

bool HardwareChromeOS::SetMaxFirmwareKeyRollforward(
    int firmware_max_rollforward) {
  // Not all devices have this field yet. So first try to read
  // it and if there is an error just fail.
  if (GetMaxFirmwareKeyRollforward() == -1)
    return false;

  return VbSetSystemPropertyInt("firmware_max_rollforward",
                                firmware_max_rollforward) == 0;
}

int HardwareChromeOS::GetMinFirmwareKeyVersion() const {
  return VbGetSystemPropertyInt("tpm_fwver");
}

bool HardwareChromeOS::SetMaxKernelKeyRollforward(int kernel_max_rollforward) {
  return VbSetSystemPropertyInt("kernel_max_rollforward",
                                kernel_max_rollforward) == 0;
}

int HardwareChromeOS::GetPowerwashCount() const {
  int powerwash_count;
  base::FilePath marker_path =
      base::FilePath(kPowerwashSafeDirectory).Append(kPowerwashCountMarker);
  string contents;
  if (!utils::ReadFile(marker_path.value(), &contents))
    return -1;
  base::TrimWhitespaceASCII(contents, base::TRIM_TRAILING, &contents);
  if (!base::StringToInt(contents, &powerwash_count))
    return -1;
  return powerwash_count;
}

std::string HardwareChromeOS::GeneratePowerwashCommand(
    bool save_rollback_data) const {
  std::string powerwash_command =
      save_rollback_data ? kRollbackPowerwashCommand : kPowerwashCommand;
#if USE_LVM_STATEFUL_PARTITION
  brillo::LogicalVolumeManager lvm;
  if (SystemState::Get()->boot_control()->IsLvmStackEnabled(&lvm)) {
    powerwash_command =
        base::JoinString({kPowerwashPreserveLVs, powerwash_command}, " ");
  } else {
    LOG(WARNING) << "LVM stack is not enabled, skipping "
                 << kPowerwashPreserveLVs << " during powerwash.";
  }
#endif  // USE_LVM_STATEFUL_PARTITION
  return powerwash_command;
}

bool HardwareChromeOS::SchedulePowerwash(bool save_rollback_data) {
  if (save_rollback_data) {
    if (!utils::WriteFile(kRollbackSaveMarkerFile, nullptr, 0)) {
      PLOG(ERROR) << "Error in creating rollback save marker file: "
                  << kRollbackSaveMarkerFile << ". Rollback will not"
                  << " preserve any data.";
    } else {
      LOG(INFO) << "Rollback data save has been scheduled on next shutdown.";
    }
  }

  auto powerwash_command = GeneratePowerwashCommand(save_rollback_data);
  const std::string powerwash_marker_full_path =
      GetPowerwashMarkerFullPath().value();
  bool result = utils::WriteFile(powerwash_marker_full_path.c_str(),
                                 powerwash_command.data(),
                                 powerwash_command.size());
  if (result) {
    LOG(INFO) << "Created " << powerwash_marker_full_path
              << " to powerwash on next reboot ("
              << "save_rollback_data=" << save_rollback_data << ")";
  } else {
    PLOG(ERROR) << "Error in creating powerwash marker file: "
                << powerwash_marker_full_path;
  }

  return result;
}

std::optional<bool> HardwareChromeOS::IsPowerwashScheduledByUpdateEngine()
    const {
  const std::string powerwash_marker_full_path =
      GetPowerwashMarkerFullPath().value();

  if (!utils::FileExists(powerwash_marker_full_path.c_str())) {
    return std::nullopt;
  }

  std::string contents;
  if (!utils::ReadFile(powerwash_marker_full_path, &contents)) {
    LOG(ERROR) << "Failed to read the powerwash marker file.";
    return false;
  }

  return contents.find(kPowerwashReasonUpdateEngineTag) != std::string::npos;
}

bool HardwareChromeOS::CancelPowerwash() {
  const base::FilePath powerwash_marker_full_path =
      GetPowerwashMarkerFullPath();
  bool result = base::DeleteFile(powerwash_marker_full_path);

  if (result) {
    LOG(INFO) << "Successfully deleted the powerwash marker file : "
              << powerwash_marker_full_path;
  } else {
    PLOG(ERROR) << "Could not delete the powerwash marker file : "
                << powerwash_marker_full_path;
  }

  // Delete the rollback save marker file if it existed.
  if (!base::DeleteFile(base::FilePath(kRollbackSaveMarkerFile))) {
    PLOG(ERROR) << "Could not remove rollback save marker";
  }

  return result;
}

bool HardwareChromeOS::GetNonVolatileDirectory(base::FilePath* path) const {
  *path = non_volatile_path_;
  return true;
}

bool HardwareChromeOS::GetRecoveryKeyVersion(std::string* version) {
  // Returned the cached value to read once per boot if read successfully.
  if (!recovery_key_version_.empty()) {
    *version = recovery_key_version_;
    return true;
  }

  // Clear for safety.
  version->clear();

  base::FilePath non_volatile_path;
  if (!GetNonVolatileDirectory(&non_volatile_path)) {
    LOG(ERROR) << "Failed to get non-volatile path.";
    return false;
  }
  auto recovery_key_version_path =
      non_volatile_path.Append(constants::kRecoveryKeyVersionFileName);

  // Use temporary version string to return empty string on read failure.
  string tmp_version;
  if (!base::ReadFileToString(recovery_key_version_path, &tmp_version)) {
    LOG(ERROR) << "Failed to read recovery key version file at: "
               << recovery_key_version_path.value();
    return false;
  }
  base::TrimWhitespaceASCII(tmp_version, base::TRIM_ALL, &tmp_version);

  // Check that the version is a valid string of integer.
  int x;
  if (!base::StringToInt(tmp_version, &x)) {
    LOG(ERROR) << "Recovery key version file does not hold a valid version: "
               << tmp_version;
    return false;
  }

  // Only perfect conversions above return true, so safe to return the string
  // itself without using `NumberToString(...)` or alike.
  *version = tmp_version;
  return true;
}

bool HardwareChromeOS::GetPowerwashSafeDirectory(base::FilePath* path) const {
  *path = base::FilePath(kPowerwashSafeDirectory);
  return true;
}

int64_t HardwareChromeOS::GetBuildTimestamp() const {
  // TODO(senj): implement this in Chrome OS.
  return 0;
}

void HardwareChromeOS::LoadConfig(const string& root_prefix, bool normal_mode) {
  brillo::KeyValueStore store;

  if (normal_mode) {
    store.Load(base::FilePath(root_prefix + kConfigFilePath));
  } else {
    if (store.Load(base::FilePath(root_prefix + kStatefulPartition +
                                  kConfigFilePath))) {
      LOG(INFO) << "UpdateManager Config loaded from stateful partition.";
    } else {
      store.Load(base::FilePath(root_prefix + kConfigFilePath));
    }
  }

  if (!store.GetBoolean(kConfigOptsIsOOBEEnabled, &is_oobe_enabled_))
    is_oobe_enabled_ = true;  // Default value.
}

bool HardwareChromeOS::GetFirstActiveOmahaPingSent() const {
  string active_ping_str;
  if (!utils::GetVpdValue(kActivePingKey, &active_ping_str)) {
    return false;
  }

  int active_ping;
  if (active_ping_str.empty() ||
      !base::StringToInt(active_ping_str, &active_ping)) {
    LOG(INFO) << "Failed to parse active_ping value: " << active_ping_str;
    return false;
  }
  return static_cast<bool>(active_ping);
}

bool HardwareChromeOS::SetFirstActiveOmahaPingSent() {
  int exit_code = 0;
  string output, error;
  vector<string> vpd_set_cmd = {
      "vpd", "-i", "RW_VPD", "-s", string(kActivePingKey) + "=1"};
  if (!Subprocess::SynchronousExec(vpd_set_cmd, &exit_code, &output, &error) ||
      exit_code) {
    LOG(ERROR) << "Failed to set vpd key for " << kActivePingKey
               << " with exit code: " << exit_code << " with output: " << output
               << " and error: " << error;
    return false;
  } else if (!error.empty()) {
    LOG(INFO) << "vpd succeeded but with error logs: " << error;
  }

  vector<string> vpd_dump_cmd = {"dump_vpd_log", "--force"};
  if (!Subprocess::SynchronousExec(vpd_dump_cmd, &exit_code, &output, &error) ||
      exit_code) {
    LOG(ERROR) << "Failed to cache " << kActivePingKey << " using dump_vpd_log"
               << " with exit code: " << exit_code << " with output: " << output
               << " and error: " << error;
    return false;
  } else if (!error.empty()) {
    LOG(INFO) << "dump_vpd_log succeeded but with error logs: " << error;
  }
  return true;
}

std::string HardwareChromeOS::GetActivateDate() const {
  std::string activate_date;
  if (!utils::GetVpdValue(kActivateDateVpdKey, &activate_date)) {
    return "";
  }
  return activate_date;
}

std::string HardwareChromeOS::GetFsiVersion() const {
  std::string fsi_version;
  if (!utils::GetVpdValue(kFsiVersionVpdKey, &fsi_version)) {
    return "";
  }
  return fsi_version;
}

std::unique_ptr<base::Value> HardwareChromeOS::ReadLocalState() const {
  base::FilePath local_state_file = base::FilePath(kLocalStatePath);

  JSONFileValueDeserializer deserializer(local_state_file);

  int error_code;
  std::string error_msg;
  std::unique_ptr<base::Value> root =
      deserializer.Deserialize(&error_code, &error_msg);

  if (!root) {
    if (error_code != 0) {
      LOG(ERROR) << "Unable to deserialize Local State with exit code: "
                 << error_code << " and error: " << error_msg;
    }
    return nullptr;
  }

  return root;
}

// Check for given given Local State the value of the enrollment
// recovery mode. Returns true if Recoverymode is set on CrOS.
bool HardwareChromeOS::IsEnrollmentRecoveryModeEnabled(
    const base::Value* local_state) const {
  if (!local_state) {
    return false;
  }
  auto& local_state_dict = local_state->GetDict();
  auto* path = local_state_dict.FindByDottedPath(kEnrollmentRecoveryRequired);

  if (!path || !path->is_bool()) {
    LOG(INFO) << "EnrollmentRecoveryRequired path does not exist in"
              << "Local State or is incorrectly formatted.";
    return false;
  }

  return path->GetBool();
}

// Check for given given Local State the value of the consumer
// segment. Returns true if IsConsumerSegement is set on CrOS.
bool HardwareChromeOS::IsConsumerSegmentSet(
    const base::Value* local_state) const {
  if (!local_state) {
    return false;
  }

  auto& local_state_dict = local_state->GetDict();
  auto* path = local_state_dict.FindByDottedPath(kConsumerSegment);

  if (!path) {
    LOG(INFO) << "IsConsumerSegment path does not exist in Local State.";
    return false;
  }

  if (!path->is_bool()) {
    LOG(INFO) << "IsConsumerSegment is incorrectly formatted in Local State.";
    return false;
  }

  return path->GetBool();
}

int HardwareChromeOS::GetActiveMiniOsPartition() const {
  char value_buffer[VB_MAX_STRING_PROPERTY];
  if (VbGetSystemPropertyString(
          kMiniOsPriorityFlag, value_buffer, sizeof(value_buffer)) == -1) {
    LOG(WARNING) << "Unable to get the active MiniOS partition from "
                 << kMiniOsPriorityFlag << ", defaulting to MINIOS-A.";
    return 0;
  }
  return (std::string(value_buffer) == "A") ? 0 : 1;
}

bool HardwareChromeOS::SetActiveMiniOsPartition(int active_partition) {
  std::string partition = active_partition == 0 ? "A" : "B";
  return VbSetSystemPropertyString(kMiniOsPriorityFlag, partition.c_str()) == 0;
}

void HardwareChromeOS::SetWarmReset(bool warm_reset) {}

std::string HardwareChromeOS::GetVersionForLogging(
    const std::string& partition_name) const {
  // TODO(zhangkelvin) Implement per-partition timestamp for Chrome OS.
  return "";
}

ErrorCode HardwareChromeOS::IsPartitionUpdateValid(
    const std::string& partition_name, const std::string& new_version) const {
  // TODO(zhangkelvin) Implement per-partition timestamp for Chrome OS.
  return ErrorCode::kSuccess;
}

bool HardwareChromeOS::IsRootfsVerificationEnabled() const {
  std::string kernel_cmd_line;
  if (!base::ReadFileToString(base::FilePath(root_.Append(kKernelCmdline)),
                              &kernel_cmd_line)) {
    LOG(ERROR) << "Can't read kernel commandline options.";
    return false;
  }
  return kernel_cmd_line.find("dm_verity.dev_wait=1") != std::string::npos;
}

bool HardwareChromeOS::ResetFWTryNextSlot() {
  const std::optional<std::string> main_fw_act = GetMainFWAct();
  const int fw_try_count = 0;

  if (!main_fw_act) {
    return false;
  }

  return SetFWTryNextSlot(*main_fw_act) && SetFWResultSuccessful() &&
         SetFWTryCount(fw_try_count);
}

bool HardwareChromeOS::SetFWTryNextSlot(base::StringPiece target_slot) {
  DCHECK(crossystem_);

  if (target_slot != kFWSlotA && target_slot != kFWSlotB) {
    LOG(ERROR) << "Invalid target_slot " << target_slot;
    return false;
  }

  if (!crossystem_->VbSetSystemPropertyString(kFWTryNextFlag,
                                              target_slot.data())) {
    LOG(ERROR) << "Unable to set " << kFWTryNextFlag << " to "
               << target_slot.data();
    return false;
  }

  return true;
}

std::optional<std::string> HardwareChromeOS::GetMainFWAct() const {
  DCHECK(crossystem_);

  const std::optional<std::string> main_fw_act =
      crossystem_->VbGetSystemPropertyString(kMainFWActFlag);
  if (!main_fw_act) {
    LOG(ERROR) << "Unable to get a current FW slot from " << kMainFWActFlag;
    return std::nullopt;
  }

  return *main_fw_act;
}

bool HardwareChromeOS::SetFWResultSuccessful() {
  DCHECK(crossystem_);

  if (!crossystem_->VbSetSystemPropertyString(kFWResultFlag, "success")) {
    LOG(ERROR) << "Unable to set " << kFWResultFlag << " to success";
    return false;
  }

  return true;
}

bool HardwareChromeOS::SetFWTryCount(int count) {
  DCHECK(crossystem_);

  if (!crossystem_->VbSetSystemPropertyInt(kFWTryCountFlag, count)) {
    LOG(ERROR) << "Unable to set " << kFWTryCountFlag << " to " << count;
    return false;
  }

  return true;
}

base::FilePath HardwareChromeOS::GetPowerwashMarkerFullPath() const {
  return root_.Append(kPowerwashMarkerPath);
}

}  // namespace chromeos_update_engine
