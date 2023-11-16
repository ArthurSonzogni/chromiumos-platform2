//
// Copyright (C) 2015 The Android Open Source Project
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

#include "update_engine/cros/boot_control_chromeos.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#if USE_LVM_STATEFUL_PARTITION
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/lvm_device.h>
#endif  // USE_LVM_STATEFUL_PARTITION
#include <chromeos/constants/imageloader.h>
#include <rootdev/rootdev.h>

extern "C" {
#include <vboot/vboot_host.h>
}

#include "update_engine/common/boot_control.h"
#include "update_engine/common/dynamic_partition_control_stub.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"

using std::string;
using std::vector;

namespace {

const char* kChromeOSPartitionNameKernel = "kernel";
const char* kChromeOSPartitionNameRoot = "root";
const char* kChromeOSPartitionNameMiniOS = "minios";
const char* kAndroidPartitionNameKernel = "boot";
const char* kAndroidPartitionNameRoot = "system";

// TODO(kimjae): Create constants/enum values for partitions in system_api.
const int kMiniOsPartitionANum = 9;

const char kPartitionNamePrefixDlc[] = "dlc";
const char kPartitionNameDlcA[] = "dlc_a";
const char kPartitionNameDlcB[] = "dlc_b";
const char kPartitionNameDlcImage[] = "dlc.img";

const char kMiniOSLabelA[] = "MINIOS-A";

constexpr char kSetGoodKernel[] = "/usr/sbin/chromeos-setgoodkernel";

// Returns the currently booted rootfs partition. "/dev/sda3", for example.
string GetBootDevice() {
  char boot_path[PATH_MAX];
  // Resolve the boot device path fully, including dereferencing through
  // dm-verity.
  int ret = rootdev(boot_path, sizeof(boot_path), true, false);
  if (ret < 0) {
    LOG(ERROR) << "rootdev failed to find the root device";
    return "";
  }
  LOG_IF(WARNING, ret > 0) << "rootdev found a device name with no device node";

  // This local variable is used to construct the return string and is not
  // passed around after use.
  return boot_path;
}

// ExecCallback called when the execution of setgoodkernel finishes. Notifies
// the caller of MarkBootSuccessfullAsync() by calling |callback| with the
// result.
void OnMarkBootSuccessfulDone(base::OnceCallback<void(bool)> callback,
                              int return_code,
                              const string& output) {
  std::move(callback).Run(return_code == 0);
}

// Will return the partition corresponding to slot B to update into Slot A.
// Empty on error.
string GetBootDeviceForMiniOs() {
  int exit_code = 0;
  string boot_device, error;
  if (!chromeos_update_engine::Subprocess::SynchronousExec(
          {"/usr/bin/root_partition_for_recovery"}, &exit_code, &boot_device,
          &error)) {
    LOG(ERROR)
        << "Failed to get the root partition name. Returned with exit code: "
        << exit_code << " and error: " << error;
    return "";
  }
  base::TrimString(boot_device, " \n", &boot_device);
  LOG(INFO) << "Running in MiniOs, set boot device to: " << boot_device;
  return boot_device;
}

// Use macros as it's LVM stateful partition specific.
#if USE_LVM_STATEFUL_PARTITION
std::string DlcLogicalVolumeName(
    const std::string& dlc_id,
    chromeos_update_engine::BootControlInterface::Slot slot) {
  return "dlc_" + dlc_id + (slot == 0 ? "_a" : "_b");
}
#endif  // USE_LVM_STATEFUL_PARTITION

}  // namespace

namespace chromeos_update_engine {

const char kMiniOSVersionKey[] = "cros_minios_version";

namespace boot_control {

// Factory defined in boot_control.h.
std::unique_ptr<BootControlInterface> CreateBootControl() {
  std::unique_ptr<BootControlChromeOS> boot_control_chromeos(
      new BootControlChromeOS());
  if (!boot_control_chromeos->Init()) {
    LOG(ERROR) << "Ignoring BootControlChromeOS failure. We won't run updates.";
  }
  return std::move(boot_control_chromeos);
}

}  // namespace boot_control

bool BootControlChromeOS::Init() {
  string boot_device;
  if (SystemState::Get()->hardware()->IsRunningFromMiniOs()) {
    // Unable to get a boot device from rootdev when in recovery mode.
    boot_device = GetBootDeviceForMiniOs();
  } else {
    boot_device = GetBootDevice();
  }

  if (boot_device.empty())
    return false;

  int partition_num;
  if (!utils::SplitPartitionName(boot_device, &boot_disk_name_, &partition_num))
    return false;

  // All installed Chrome OS devices have two slots. We don't update removable
  // devices, so we will pretend we have only one slot in that case.
  if (IsRemovableDevice(boot_disk_name_)) {
    LOG(INFO)
        << "Booted from a removable device, pretending we have only one slot.";
    num_slots_ = 1;
  } else {
    // TODO(deymo): Look at the actual number of slots reported in the GPT.
    num_slots_ = 2;
  }

  // Search through the slots to see which slot has the partition_num we booted
  // from. This should map to one of the existing slots, otherwise something is
  // very wrong.
  current_slot_ = 0;
  while (current_slot_ < num_slots_ &&
         partition_num !=
             GetPartitionNumber(kChromeOSPartitionNameRoot, current_slot_)) {
    current_slot_++;
  }
  if (current_slot_ >= num_slots_) {
    LOG(ERROR) << "Couldn't find the slot number corresponding to the "
               << "partition " << boot_device
               << ", number of slots: " << num_slots_
               << ". This device is not updateable.";
    num_slots_ = 1;
    current_slot_ = BootControlInterface::kInvalidSlot;
    return false;
  }

  dynamic_partition_control_.reset(new DynamicPartitionControlStub());

  LOG(INFO) << "Booted from slot " << current_slot_ << " (slot "
            << SlotName(current_slot_) << ") of " << num_slots_
            << " slots present on disk " << boot_disk_name_;
  return true;
}

unsigned int BootControlChromeOS::GetNumSlots() const {
  return num_slots_;
}

BootControlInterface::Slot BootControlChromeOS::GetCurrentSlot() const {
  return current_slot_;
}

BootControlInterface::Slot BootControlChromeOS::GetFirstInactiveSlot() const {
  if (GetCurrentSlot() == BootControlInterface::kInvalidSlot ||
      GetNumSlots() < 2)
    return BootControlInterface::kInvalidSlot;

  for (Slot slot = 0; slot < GetNumSlots(); slot++) {
    if (slot != GetCurrentSlot())
      return slot;
  }
  return BootControlInterface::kInvalidSlot;
}

base::FilePath BootControlChromeOS::GetBootDevicePath() const {
  return base::FilePath{boot_disk_name_};
}

bool BootControlChromeOS::ParseDlcPartitionName(
    const std::string partition_name,
    std::string* dlc_id,
    std::string* dlc_package) const {
  CHECK_NE(dlc_id, nullptr);
  CHECK_NE(dlc_package, nullptr);

  vector<string> tokens = base::SplitString(
      partition_name, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (tokens.size() != 3 || tokens[0] != kPartitionNamePrefixDlc) {
    LOG(ERROR) << "DLC partition name (" << partition_name
               << ") is not well formatted.";
    return false;
  }
  if (tokens[1].empty() || tokens[2].empty()) {
    LOG(ERROR) << " partition name does not contain valid DLC ID (" << tokens[1]
               << ") or package (" << tokens[2] << ")";
    return false;
  }

  *dlc_id = tokens[1];
  *dlc_package = tokens[2];
  return true;
}

bool BootControlChromeOS::GetPartitionDevice(const std::string& partition_name,
                                             BootControlInterface::Slot slot,
                                             bool not_in_payload,
                                             std::string* device,
                                             bool* is_dynamic) const {
  // Partition name prefixed with |kPartitionNamePrefixDlc| is a DLC module.
  if (base::StartsWith(partition_name, kPartitionNamePrefixDlc,
                       base::CompareCase::SENSITIVE)) {
    string dlc_id, dlc_package;
    if (!ParseDlcPartitionName(partition_name, &dlc_id, &dlc_package))
      return false;

    *device = base::FilePath(imageloader::kDlcImageRootpath)
                  .Append(dlc_id)
                  .Append(dlc_package)
                  .Append(slot == 0 ? kPartitionNameDlcA : kPartitionNameDlcB)
                  .Append(kPartitionNameDlcImage)
                  .value();
#if USE_LVM_STATEFUL_PARTITION
    // Override with logical volume path if valid.
    // DLC logical volumes follow a specific naming scheme.
    brillo::LogicalVolumeManager lvm;
    std::string lv_name = DlcLogicalVolumeName(dlc_id, slot);
    // Stateful is always partition number 1 in CrOS.
    auto stateful_part = utils::MakePartitionName(boot_disk_name_, 1);
    if (auto pv = lvm.GetPhysicalVolume(base::FilePath(stateful_part));
        !pv || !pv->IsValid()) {
      LOG(WARNING) << "Could not get physical volume from " << stateful_part;
    } else if (auto vg = lvm.GetVolumeGroup(*pv); !vg || !vg->IsValid()) {
      LOG(WARNING) << "Could not get volume group from "
                   << pv->GetPath().value();
    } else if (auto lv = lvm.GetLogicalVolume(*vg, lv_name);
               !lv || !lv->IsValid()) {
      LOG(WARNING) << "Could not get logical volume (" << lv_name << ") from "
                   << vg->GetName();
    } else {
      auto lv_path = lv->GetPath().value();
      LOG(INFO) << "Overriding to logical volume path at " << lv_path;
      *device = lv_path;
    }
#endif  // USE_LVM_STATEFUL_PARTITION
    return true;
  }
  int partition_num = GetPartitionNumber(partition_name, slot);
  if (partition_num < 0)
    return false;

  string part_device = utils::MakePartitionName(boot_disk_name_, partition_num);
  if (part_device.empty())
    return false;

  *device = part_device;
  if (is_dynamic) {
    *is_dynamic = false;
  }
  return true;
}

bool BootControlChromeOS::GetPartitionDevice(const string& partition_name,
                                             BootControlInterface::Slot slot,
                                             string* device) const {
  return GetPartitionDevice(partition_name, slot, false, device, nullptr);
}

bool BootControlChromeOS::GetErrorCounter(BootControlInterface::Slot slot,
                                          int* error_counter) const {
  int partition_num = GetPartitionNumber(kChromeOSPartitionNameKernel, slot);
  if (partition_num < 0)
    return false;

  CgptAddParams params;
  memset(&params, '\0', sizeof(params));
  params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  params.partition = partition_num;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return false;

  *error_counter = params.error_counter;
  return true;
}

bool BootControlChromeOS::SetErrorCounter(BootControlInterface::Slot slot,
                                          int error_counter) {
  int partition_num = GetPartitionNumber(kChromeOSPartitionNameKernel, slot);
  if (partition_num < 0)
    return false;

  CgptAddParams add_params;
  memset(&add_params, 0, sizeof(add_params));

  add_params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  add_params.partition = partition_num;

  add_params.error_counter = error_counter;
  add_params.set_error_counter = 1;

  int retval = CgptSetAttributes(&add_params);
  if (retval != CGPT_OK) {
    LOG(ERROR) << "Unable to set error_counter to " << add_params.tries
               << " for slot " << SlotName(slot) << " (partition "
               << partition_num << ").";
    return false;
  }
  return true;
}

bool BootControlChromeOS::IsSlotBootable(Slot slot) const {
  int partition_num = GetPartitionNumber(kChromeOSPartitionNameKernel, slot);
  if (partition_num < 0)
    return false;

  CgptAddParams params;
  memset(&params, '\0', sizeof(params));
  params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  params.partition = partition_num;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return false;

  return params.successful || params.tries > 0;
}

bool BootControlChromeOS::MarkSlotUnbootable(Slot slot) {
  LOG(INFO) << "Marking slot " << SlotName(slot) << " unbootable";

  if (slot == current_slot_) {
    LOG(ERROR) << "Refusing to mark current slot as unbootable.";
    return false;
  }

  int partition_num = GetPartitionNumber(kChromeOSPartitionNameKernel, slot);
  if (partition_num < 0)
    return false;

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  params.partition = partition_num;

  params.successful = false;
  params.set_successful = true;

  params.tries = 0;
  params.set_tries = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK) {
    LOG(ERROR) << "Marking kernel unbootable failed.";
    return false;
  }

  return true;
}

bool BootControlChromeOS::SetActiveBootSlot(Slot slot) {
  LOG(INFO) << "Marking slot " << SlotName(slot) << " active.";

  int partition_num = GetPartitionNumber(kChromeOSPartitionNameKernel, slot);
  if (partition_num < 0)
    return false;

  CgptPrioritizeParams prio_params;
  memset(&prio_params, 0, sizeof(prio_params));

  prio_params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  prio_params.set_partition = partition_num;

  prio_params.max_priority = 0;

  int retval = CgptPrioritize(&prio_params);
  if (retval != CGPT_OK) {
    LOG(ERROR) << "Unable to set highest priority for slot " << SlotName(slot)
               << " (partition " << partition_num << ").";
    return false;
  }

  CgptAddParams add_params;
  memset(&add_params, 0, sizeof(add_params));

  add_params.drive_name = const_cast<char*>(boot_disk_name_.c_str());
  add_params.partition = partition_num;

  add_params.tries = 6;
  add_params.set_tries = true;

  retval = CgptSetAttributes(&add_params);
  if (retval != CGPT_OK) {
    LOG(ERROR) << "Unable to set NumTriesLeft to " << add_params.tries
               << " for slot " << SlotName(slot) << " (partition "
               << partition_num << ").";
    return false;
  }

  return true;
}

bool BootControlChromeOS::MarkBootSuccessful() {
  int ret;
  string out, err;
  if (!Subprocess::Get().SynchronousExec({kSetGoodKernel}, &ret, &out, &err) ||
      ret != 0) {
    LOG(ERROR) << "Failed to setgoodkernel, returncode=" << ret
               << " stdout=" << out << " stderr=" << err;
    return false;
  }
  return true;
}

bool BootControlChromeOS::MarkBootSuccessfulAsync(
    base::OnceCallback<void(bool)> callback) {
  return Subprocess::Get().Exec({kSetGoodKernel},
                                base::BindOnce(&OnMarkBootSuccessfulDone,
                                               std::move(callback))) != 0;
}

// static
string BootControlChromeOS::SysfsBlockDevice(const string& device) {
  base::FilePath device_path(device);
  if (device_path.DirName().value() != "/dev") {
    return "";
  }
  return base::FilePath("/sys/block").Append(device_path.BaseName()).value();
}

// static
bool BootControlChromeOS::IsRemovableDevice(const string& device) {
  string sysfs_block = SysfsBlockDevice(device);
  string removable;
  if (sysfs_block.empty() ||
      !base::ReadFileToString(base::FilePath(sysfs_block).Append("removable"),
                              &removable)) {
    return false;
  }
  base::TrimWhitespaceASCII(removable, base::TRIM_ALL, &removable);
  return removable == "1";
}

int BootControlChromeOS::GetPartitionNumber(
    const string partition_name, BootControlInterface::Slot slot) const {
  if (slot >= num_slots_) {
    LOG(ERROR) << "Invalid slot number: " << slot << ", we only have "
               << num_slots_ << " slot(s)";
    return -1;
  }

  // In Chrome OS, the partition numbers are hard-coded:
  // KERNEL-A=2, ROOT-A=3, KERNEL-B=4, ROOT-B=4, MINIOS-A=9, MINIOS-B=10
  // To help compatibility between different we accept both lowercase and
  // uppercase names in the ChromeOS or Brillo standard names.
  // See http://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
  string partition_lower = base::ToLowerASCII(partition_name);
  int base_part_num = 2 + 2 * slot;
  if (partition_lower == kChromeOSPartitionNameKernel ||
      partition_lower == kAndroidPartitionNameKernel)
    return base_part_num + 0;
  if (partition_lower == kChromeOSPartitionNameRoot ||
      partition_lower == kAndroidPartitionNameRoot)
    return base_part_num + 1;
  if (partition_lower == kChromeOSPartitionNameMiniOS) {
    return slot + kMiniOsPartitionANum;
  }
  LOG(ERROR) << "Unknown Chrome OS partition name \"" << partition_name << "\"";
  return -1;
}

bool BootControlChromeOS::IsSlotMarkedSuccessful(Slot slot) const {
  LOG(ERROR) << __func__ << " not supported.";
  return false;
}

DynamicPartitionControlInterface*
BootControlChromeOS::GetDynamicPartitionControl() {
  return dynamic_partition_control_.get();
}

std::string BootControlChromeOS::GetMiniOSPartitionName() {
  int active_minios_partition_number =
      SystemState::Get()->hardware()->GetActiveMiniOsPartition();

  // Get the full partition path.
  auto partition = utils::MakePartitionName(
      boot_disk_name_, active_minios_partition_number + kMiniOsPartitionANum);
  return string(partition);
}

bool BootControlChromeOS::GetMiniOSKernelConfig(std::string* configs) {
  vector<string> dump_cmd = {"dump_kernel_config", GetMiniOSPartitionName()};

  int exit_code = 0;
  string error;
  if (!Subprocess::SynchronousExec(dump_cmd, &exit_code, configs, &error) ||
      exit_code) {
    LOG(ERROR) << "Failed getting kernel configs with exit code: " << exit_code
               << " with output: " << configs << " and error: " << error;
    configs->clear();
    return false;
  } else if (!error.empty()) {
    LOG(INFO) << "succeeded getting the configs but with error logs: " << error;
  }
  return true;
}

bool BootControlChromeOS::GetMiniOSVersion(const std::string& kernel_output,
                                           std::string* value) {
  value->clear();
  auto key_start = kernel_output.find(std::string(kMiniOSVersionKey) + "=");
  if (key_start == std::string::npos) {
    return false;
  }

  auto value_start = key_start + sizeof(kMiniOSVersionKey);

  // Find the first break point and split the string.
  std::size_t value_end = kernel_output.find_first_of(" \"", value_start);
  *value = kernel_output.substr(value_start, value_end - value_start);
  if (value->empty()) {
    LOG(INFO) << "Value not found for key " << kMiniOSVersionKey << "  in "
              << kernel_output;
    return false;
  }
  return true;
}

bool BootControlChromeOS::SupportsMiniOSPartitions() {
  // Checking for the MINIOS-A partition label should be enough since MINIOS-B
  // will always be on the device if A is, hardcoded as partitions 9 and 10.
  CgptFindParams cgpt_params = {.set_label = 1, .label = kMiniOSLabelA};
  CgptFind(&cgpt_params);
  return cgpt_params.hits == 1;
}

bool BootControlChromeOS::IsLvmStackEnabled(brillo::LogicalVolumeManager* lvm) {
  if (!is_lvm_stack_enabled_.has_value()) {
    // Cache the value.
    // Stateful partition will always be in partition 1 in CrOS.
    auto pv = lvm->GetPhysicalVolume(base::FilePath(
        utils::MakePartitionName(GetBootDevicePath().value(), 1)));
    is_lvm_stack_enabled_ = pv.has_value() && pv->IsValid();
  }
  return is_lvm_stack_enabled_.value();
}

}  // namespace chromeos_update_engine
