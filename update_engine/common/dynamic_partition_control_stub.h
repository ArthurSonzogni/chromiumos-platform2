// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_STUB_H_
#define UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_STUB_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "update_engine/common/dynamic_partition_control_interface.h"

namespace chromeos_update_engine {

class DynamicPartitionControlStub : public DynamicPartitionControlInterface {
 public:
  FeatureFlag GetDynamicPartitionsFeatureFlag() override;
  FeatureFlag GetVirtualAbFeatureFlag() override;
  bool OptimizeOperation(const std::string& partition_name,
                         const InstallOperation& operation,
                         InstallOperation* optimized) override;
  void Cleanup() override;
  bool PreparePartitionsForUpdate(uint32_t source_slot,
                                  uint32_t target_slot,
                                  const DeltaArchiveManifest& manifest,
                                  bool update,
                                  uint64_t* required_size) override;

  bool FinishUpdate(bool powerwash_required) override;
  std::unique_ptr<AbstractAction> GetCleanupPreviousUpdateAction(
      BootControlInterface* boot_control,
      PrefsInterface* prefs,
      CleanupPreviousUpdateActionDelegateInterface* delegate) override;
  bool ResetUpdate(PrefsInterface* prefs) override;

  bool ListDynamicPartitionsForSlot(
      uint32_t current_slot, std::vector<std::string>* partitions) override;
  bool GetDeviceDir(std::string* path) override;

  bool VerifyExtentsForUntouchedPartitions(
      uint32_t source_slot,
      uint32_t target_slot,
      const std::vector<std::string>& partitions) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_STUB_H_
