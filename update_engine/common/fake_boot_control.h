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

#ifndef UPDATE_ENGINE_COMMON_FAKE_BOOT_CONTROL_H_
#define UPDATE_ENGINE_COMMON_FAKE_BOOT_CONTROL_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <brillo/blkdev_utils/lvm.h>

#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/dynamic_partition_control_stub.h"

namespace chromeos_update_engine {

// Implements a fake bootloader control interface used for testing.
class FakeBootControl : public BootControlInterface {
 public:
  FakeBootControl() {
    SetNumSlots(num_slots_);
    // The current slot should be bootable.
    is_bootable_[current_slot_] = true;

    dynamic_partition_control_.reset(new DynamicPartitionControlStub());
  }
  FakeBootControl(const FakeBootControl&) = delete;
  FakeBootControl& operator=(const FakeBootControl&) = delete;

  // BootControlInterface overrides.
  unsigned int GetNumSlots() const override { return num_slots_; }
  BootControlInterface::Slot GetCurrentSlot() const override {
    return current_slot_;
  }
  BootControlInterface::Slot GetFirstInactiveSlot() const override {
    return first_inactive_slot_;
  }

  bool GetPartitionDevice(const std::string& partition_name,
                          BootControlInterface::Slot slot,
                          bool not_in_payload,
                          std::string* device,
                          bool* is_dynamic) const override {
    if (slot >= num_slots_)
      return false;
    auto part_it = devices_[slot].find(partition_name);
    if (part_it == devices_[slot].end())
      return false;
    *device = part_it->second;
    if (is_dynamic != nullptr) {
      *is_dynamic = false;
    }
    return true;
  }

  bool GetPartitionDevice(const std::string& partition_name,
                          BootControlInterface::Slot slot,
                          std::string* device) const override {
    return GetPartitionDevice(partition_name, slot, false, device, nullptr);
  }

  bool GetErrorCounter(Slot slot, int* error_counter) const override {
    *error_counter = error_counter_;
    return true;
  }

  bool SetErrorCounter(Slot slot, int error_counter) override {
    error_counter_ = error_counter;
    return true;
  }

  bool IsSlotBootable(BootControlInterface::Slot slot) const override {
    return slot < num_slots_ && is_bootable_[slot];
  }

  bool MarkSlotUnbootable(BootControlInterface::Slot slot) override {
    if (slot >= num_slots_)
      return false;
    is_bootable_[slot] = false;
    return true;
  }

  bool SetActiveBootSlot(Slot slot) override { return true; }

  bool MarkBootSuccessful() override {
    is_marked_successful_[GetCurrentSlot()] = true;
    return true;
  }

  bool MarkBootSuccessfulAsync(
      base::OnceCallback<void(bool)> callback) override {
    // We run the callback directly from here to avoid having to setup a message
    // loop in the test environment.
    is_marked_successful_[GetCurrentSlot()] = true;
    std::move(callback).Run(true);
    return true;
  }

  bool IsSlotMarkedSuccessful(Slot slot) const override {
    return slot < num_slots_ && is_marked_successful_[slot];
  }

  // Setters
  void SetNumSlots(unsigned int num_slots) {
    num_slots_ = num_slots;
    is_bootable_.resize(num_slots_, false);
    is_marked_successful_.resize(num_slots_, false);
    devices_.resize(num_slots_);
  }

  void SetCurrentSlot(BootControlInterface::Slot slot) { current_slot_ = slot; }
  void SetFirstInactiveSlot(BootControlInterface::Slot slot) {
    first_inactive_slot_ = slot;
  }

  base::FilePath GetBootDevicePath() const override { return {}; }

  void SetPartitionDevice(const std::string& partition_name,
                          BootControlInterface::Slot slot,
                          const std::string& device) {
    DCHECK(slot < num_slots_);
    devices_[slot][partition_name] = device;
  }

  void SetSlotBootable(BootControlInterface::Slot slot, bool bootable) {
    DCHECK(slot < num_slots_);
    is_bootable_[slot] = bootable;
  }

  void SetErrorCounter(int error_counter) { error_counter_ = error_counter; }

  DynamicPartitionControlInterface* GetDynamicPartitionControl() override {
    return dynamic_partition_control_.get();
  }

  bool GetMiniOSKernelConfig(std::string* configs) override { return true; }

  bool GetMiniOSVersion(const std::string& kernel_output,
                        std::string* value) override {
    return false;
  }

  std::string GetMiniOSPartitionName() override { return ""; }

  void SetSupportsMiniOSPartitions(bool is_supported) {
    supports_minios_ = is_supported;
  }
  bool SupportsMiniOSPartitions() override { return supports_minios_; }

  bool IsLvmStackEnabled(brillo::LogicalVolumeManager* lvm) override {
    return is_lvm_stack_enabled_;
  }
  void SetIsLvmStackEnabled(bool enabled) { is_lvm_stack_enabled_ = enabled; }

 private:
  BootControlInterface::Slot num_slots_{2};
  BootControlInterface::Slot current_slot_{0};
  BootControlInterface::Slot first_inactive_slot_{0};

  std::vector<bool> is_bootable_;
  std::vector<bool> is_marked_successful_;
  std::vector<std::map<std::string, std::string>> devices_;

  bool supports_minios_{false};
  int error_counter_ = 0;

  bool is_lvm_stack_enabled_{false};

  std::unique_ptr<DynamicPartitionControlInterface> dynamic_partition_control_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_FAKE_BOOT_CONTROL_H_
