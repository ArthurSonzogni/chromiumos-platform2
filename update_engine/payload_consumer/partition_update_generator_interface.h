// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_PARTITION_UPDATE_GENERATOR_INTERFACE_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_PARTITION_UPDATE_GENERATOR_INTERFACE_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "update_engine/common/boot_control_interface.h"

namespace chromeos_update_engine {
class PartitionUpdate;

// This class parses the partitions that are not included in the payload of a
// partial A/B update. And it generates additional operations for these
// partitions to make the update complete.
class PartitionUpdateGeneratorInterface {
 public:
  virtual ~PartitionUpdateGeneratorInterface() = default;

  // Adds PartitionUpdate for partitions not included in the payload. For static
  // partitions, it generates SOURCE_COPY operations to copy the bytes from the
  // source slot to target slot. For dynamic partitions, it only calculates the
  // partition hash for the filesystem verification later.
  virtual bool GenerateOperationsForPartitionsNotInPayload(
      BootControlInterface::Slot source_slot,
      BootControlInterface::Slot target_slot,
      const std::set<std::string>& partitions_in_payload,
      std::vector<PartitionUpdate>* update_list) = 0;
};

namespace partition_update_generator {
std::unique_ptr<PartitionUpdateGeneratorInterface> Create(
    BootControlInterface* boot_control, size_t block_size);
}

}  // namespace chromeos_update_engine

#endif
