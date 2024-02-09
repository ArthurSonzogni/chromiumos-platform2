// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_FULL_UPDATE_GENERATOR_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_FULL_UPDATE_GENERATOR_H_

#include <string>
#include <vector>

#include "update_engine/payload_generator/blob_file_writer.h"
#include "update_engine/payload_generator/operations_generator.h"
#include "update_engine/payload_generator/payload_generation_config.h"

namespace chromeos_update_engine {

class FullUpdateGenerator : public OperationsGenerator {
 public:
  FullUpdateGenerator() = default;
  FullUpdateGenerator(const FullUpdateGenerator&) = delete;
  FullUpdateGenerator& operator=(const FullUpdateGenerator&) = delete;

  // OperationsGenerator override.
  // Creates a full update for the target image defined in |config|. |config|
  // must be a valid payload generation configuration for a full payload.
  // Populates |aops|, with data about the update operations, and writes
  // relevant data to |blob_file|.
  bool GenerateOperations(const PayloadGenerationConfig& config,
                          const PartitionConfig& old_part,
                          const PartitionConfig& new_part,
                          BlobFileWriter* blob_file,
                          std::vector<AnnotatedOperation>* aops) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_FULL_UPDATE_GENERATOR_H_
