// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/boot_control_stub.h"
#include "update_engine/common/dynamic_partition_control_stub.h"

#include <base/logging.h>

using std::string;

namespace chromeos_update_engine {

BootControlStub::BootControlStub()
    : dynamic_partition_control_(new DynamicPartitionControlStub()) {}

unsigned int BootControlStub::GetNumSlots() const {
  return 0;
}

BootControlInterface::Slot BootControlStub::GetCurrentSlot() const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return 0;
}

BootControlInterface::Slot BootControlStub::GetFirstInactiveSlot() const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return 0;
}

base::FilePath BootControlStub::GetBootDevicePath() const {
  return base::FilePath{};
}

bool BootControlStub::GetPartitionDevice(const std::string& partition_name,
                                         BootControlInterface::Slot slot,
                                         bool not_in_payload,
                                         std::string* device,
                                         bool* is_dynamic) const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::GetPartitionDevice(const string& partition_name,
                                         Slot slot,
                                         string* device) const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::GetErrorCounter(Slot slot, int* error_counter) const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::SetErrorCounter(Slot slot, int error_counter) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::IsSlotBootable(Slot slot) const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::MarkSlotUnbootable(Slot slot) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::SetActiveBootSlot(Slot slot) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::MarkBootSuccessful() {
  return false;
}

bool BootControlStub::MarkBootSuccessfulAsync(
    base::OnceCallback<void(bool)> callback) {
  // This is expected to be called on update_engine startup.
  return false;
}

bool BootControlStub::IsSlotMarkedSuccessful(Slot slot) const {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

DynamicPartitionControlInterface*
BootControlStub::GetDynamicPartitionControl() {
  return dynamic_partition_control_.get();
}

bool BootControlStub::GetMiniOSKernelConfig(std::string* configs) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::GetMiniOSVersion(const std::string& kernel_output,
                                       std::string* value) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

std::string BootControlStub::GetMiniOSPartitionName() {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return "";
}

bool BootControlStub::SupportsMiniOSPartitions() {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

bool BootControlStub::IsLvmStackEnabled(brillo::LogicalVolumeManager* lvm) {
  LOG(ERROR) << __FUNCTION__ << " should never be called.";
  return false;
}

}  // namespace chromeos_update_engine
