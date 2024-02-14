// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_BOOT_CONTROL_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_BOOT_CONTROL_CHROMEOS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/dynamic_partition_control_interface.h"

namespace chromeos_update_engine {

extern const char kMiniOSVersionKey[];

// The Chrome OS implementation of the BootControlInterface. This interface
// assumes the partition names and numbers used in Chrome OS devices.
class BootControlChromeOS : public BootControlInterface {
 public:
  BootControlChromeOS() = default;
  BootControlChromeOS(const BootControlChromeOS&) = delete;
  BootControlChromeOS& operator=(const BootControlChromeOS&) = delete;

  ~BootControlChromeOS() = default;

  // Initialize the BootControl instance loading the constant values. Returns
  // whether the operation succeeded. In case of failure, normally meaning
  // some critical failure such as we couldn't determine the slot that we
  // booted from, the implementation will pretend that there's only one slot and
  // therefore A/B updates are disabled.
  bool Init();

  // BootControlInterface overrides.
  unsigned int GetNumSlots() const override;
  BootControlInterface::Slot GetCurrentSlot() const override;
  BootControlInterface::Slot GetFirstInactiveSlot() const override;
  base::FilePath GetBootDevicePath() const override;
  bool GetPartitionDevice(const std::string& partition_name,
                          BootControlInterface::Slot slot,
                          bool not_in_payload,
                          std::string* device,
                          bool* is_dynamic) const override;
  bool GetPartitionDevice(const std::string& partition_name,
                          BootControlInterface::Slot slot,
                          std::string* device) const override;
  bool GetErrorCounter(BootControlInterface::Slot slot,
                       int* error_counter) const override;
  bool SetErrorCounter(BootControlInterface::Slot slot,
                       int error_counter) override;
  bool IsSlotBootable(BootControlInterface::Slot slot) const override;
  bool MarkSlotUnbootable(BootControlInterface::Slot slot) override;
  bool SetActiveBootSlot(BootControlInterface::Slot slot) override;
  bool MarkBootSuccessful() override;
  bool MarkBootSuccessfulAsync(
      base::OnceCallback<void(bool)> callback) override;
  bool IsSlotMarkedSuccessful(BootControlInterface::Slot slot) const override;
  DynamicPartitionControlInterface* GetDynamicPartitionControl() override;
  bool GetMiniOSKernelConfig(std::string* configs) override;
  bool GetMiniOSVersion(const std::string& kernel_output,
                        std::string* value) override;
  std::string GetMiniOSPartitionName() override;
  bool SupportsMiniOSPartitions() override;
  bool IsLvmStackEnabled(brillo::LogicalVolumeManager* lvm) override;

 private:
  friend class BootControlChromeOSTest;
  FRIEND_TEST(BootControlChromeOSTest, GetFirstInactiveSlot);
  FRIEND_TEST(BootControlChromeOSTest, SysfsBlockDeviceTest);
  FRIEND_TEST(BootControlChromeOSTest, GetPartitionNumberTest);
  FRIEND_TEST(BootControlChromeOSTest, ParseDlcPartitionNameTest);
  FRIEND_TEST(BootControlChromeOSTest, IsLvmStackEnabledTest);

  // Returns the sysfs block device for a root block device. For example,
  // SysfsBlockDevice("/dev/sda") returns "/sys/block/sda". Returns an empty
  // string if the input device is not of the "/dev/xyz" form.
  static std::string SysfsBlockDevice(const std::string& device);

  // Returns true if the root |device| (e.g., "/dev/sdb") is known to be
  // removable, false otherwise.
  static bool IsRemovableDevice(const std::string& device);

  // Return the hard-coded partition number used in Chrome OS for the passed
  // |partition_name| and |slot|. In case of invalid data, returns -1.
  int GetPartitionNumber(const std::string partition_name,
                         BootControlInterface::Slot slot) const;

  // Extracts DLC module ID and package ID from partition name. The structure of
  // the partition name is dlc/<dlc-id>/<dlc-package>. For example:
  // dlc/fake-dlc/fake-package
  bool ParseDlcPartitionName(const std::string partition_name,
                             std::string* dlc_id,
                             std::string* dlc_package) const;

  // Get primary user's cryptohome daemon store path for DLCs.
  bool GetCryptohomeDlcPath(base::FilePath* dlc_path) const;

  // Cached values for GetNumSlots() and GetCurrentSlot().
  BootControlInterface::Slot num_slots_{1};
  BootControlInterface::Slot current_slot_{BootControlInterface::kInvalidSlot};

  // The block device of the disk we booted from, without the partition number.
  std::string boot_disk_name_;

  // Cached value for LVM stack enablement check.
  std::optional<bool> is_lvm_stack_enabled_;

  std::unique_ptr<DynamicPartitionControlInterface> dynamic_partition_control_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_BOOT_CONTROL_CHROMEOS_H_
