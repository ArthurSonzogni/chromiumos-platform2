// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <memory>
#include <string>

#include <base/logging.h>

#include "update_engine/common/dynamic_partition_control_stub.h"

namespace chromeos_update_engine {

FeatureFlag DynamicPartitionControlStub::GetDynamicPartitionsFeatureFlag() {
  return FeatureFlag(FeatureFlag::Value::NONE);
}

FeatureFlag DynamicPartitionControlStub::GetVirtualAbFeatureFlag() {
  return FeatureFlag(FeatureFlag::Value::NONE);
}

bool DynamicPartitionControlStub::OptimizeOperation(
    const std::string& partition_name,
    const InstallOperation& operation,
    InstallOperation* optimized) {
  return false;
}

void DynamicPartitionControlStub::Cleanup() {}

bool DynamicPartitionControlStub::PreparePartitionsForUpdate(
    uint32_t source_slot,
    uint32_t target_slot,
    const DeltaArchiveManifest& manifest,
    bool update,
    uint64_t* required_size) {
  return true;
}

bool DynamicPartitionControlStub::FinishUpdate(bool powerwash_required) {
  return true;
}

std::unique_ptr<AbstractAction>
DynamicPartitionControlStub::GetCleanupPreviousUpdateAction(
    BootControlInterface* boot_control,
    PrefsInterface* prefs,
    CleanupPreviousUpdateActionDelegateInterface* delegate) {
  return std::make_unique<NoOpAction>();
}

bool DynamicPartitionControlStub::ResetUpdate(PrefsInterface* prefs) {
  return false;
}

bool DynamicPartitionControlStub::ListDynamicPartitionsForSlot(
    uint32_t current_slot, std::vector<std::string>* partitions) {
  return true;
}

bool DynamicPartitionControlStub::GetDeviceDir(std::string* path) {
  return true;
}

bool DynamicPartitionControlStub::VerifyExtentsForUntouchedPartitions(
    uint32_t source_slot,
    uint32_t target_slot,
    const std::vector<std::string>& partitions) {
  return true;
}

}  // namespace chromeos_update_engine
