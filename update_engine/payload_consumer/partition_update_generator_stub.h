// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_PARTITION_UPDATE_GENERATOR_STUB_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_PARTITION_UPDATE_GENERATOR_STUB_H_

#include <set>
#include <string>
#include <vector>

#include "update_engine/common/boot_control_interface.h"
#include "update_engine/payload_consumer/partition_update_generator_interface.h"

namespace chromeos_update_engine {
class PartitionUpdateGeneratorStub : public PartitionUpdateGeneratorInterface {
 public:
  PartitionUpdateGeneratorStub() = default;
  bool GenerateOperationsForPartitionsNotInPayload(
      BootControlInterface::Slot source_slot,
      BootControlInterface::Slot target_slot,
      const std::set<std::string>& partitions_in_payload,
      std::vector<PartitionUpdate>* update_list) override;
};

}  // namespace chromeos_update_engine

#endif
