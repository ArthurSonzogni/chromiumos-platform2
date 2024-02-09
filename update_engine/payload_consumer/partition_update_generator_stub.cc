// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/partition_update_generator_stub.h"

#include <memory>

namespace chromeos_update_engine {

bool PartitionUpdateGeneratorStub::GenerateOperationsForPartitionsNotInPayload(
    chromeos_update_engine::BootControlInterface::Slot source_slot,
    chromeos_update_engine::BootControlInterface::Slot target_slot,
    const std::set<std::string>& partitions_in_payload,
    std::vector<PartitionUpdate>* update_list) {
  return true;
}

namespace partition_update_generator {
std::unique_ptr<PartitionUpdateGeneratorInterface> Create(
    BootControlInterface* boot_control, size_t block_size) {
  return std::make_unique<PartitionUpdateGeneratorStub>();
}
}  // namespace partition_update_generator

}  // namespace chromeos_update_engine
