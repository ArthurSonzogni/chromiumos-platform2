// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_OPERATIONS_GENERATOR_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_OPERATIONS_GENERATOR_H_

#include <vector>

#include "update_engine/payload_generator/annotated_operation.h"
#include "update_engine/payload_generator/blob_file_writer.h"
#include "update_engine/payload_generator/payload_generation_config.h"

namespace chromeos_update_engine {

class OperationsGenerator {
 public:
  OperationsGenerator(const OperationsGenerator&) = delete;
  OperationsGenerator& operator=(const OperationsGenerator&) = delete;
  virtual ~OperationsGenerator() = default;

  // This method generates a list of operations to update from the partition
  // |old_part| to |new_part| and stores the generated operations in |aops|.
  // These operations are generated based on the given |config|.
  // The operations should be applied in the order specified in the list, and
  // they respect the payload version and type (delta or full) specified in
  // |config|.
  // The operations generated will refer to offsets in the file |blob_file|,
  // where this function stores the output, but not necessarily in the same
  // order as they appear in the |aops|.
  virtual bool GenerateOperations(const PayloadGenerationConfig& config,
                                  const PartitionConfig& old_part,
                                  const PartitionConfig& new_part,
                                  BlobFileWriter* blob_file,
                                  std::vector<AnnotatedOperation>* aops) = 0;

 protected:
  OperationsGenerator() = default;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_OPERATIONS_GENERATOR_H_
