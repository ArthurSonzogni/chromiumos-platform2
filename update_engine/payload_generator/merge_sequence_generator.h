// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_MERGE_SEQUENCE_GENERATOR_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_MERGE_SEQUENCE_GENERATOR_H_

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "update_engine/payload_generator/annotated_operation.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/payload_generator/extent_utils.h"
#include "update_engine/update_metadata.pb.h"

namespace chromeos_update_engine {
// Constructs CowMergeOperation from src & dst extents
CowMergeOperation CreateCowMergeOperation(const Extent& src_extent,
                                          const Extent& dst_extent);

// Comparator for CowMergeOperation.
bool operator<(const CowMergeOperation& op1, const CowMergeOperation& op2);
bool operator==(const CowMergeOperation& op1, const CowMergeOperation& op2);

std::ostream& operator<<(std::ostream& os,
                         const CowMergeOperation& merge_operation);

// This class takes a list of CowMergeOperations; and sorts them so that no
// read after write will happen by following the sequence. When there is a
// cycle, we will omit some operations in the list. Therefore, the result
// sequence may not contain all blocks in the input list.
class MergeSequenceGenerator {
 public:
  // Creates an object from a list of OTA InstallOperations. Returns nullptr on
  // failure.
  static std::unique_ptr<MergeSequenceGenerator> Create(
      const std::vector<AnnotatedOperation>& aops);
  // Checks that no read after write happens in the given sequence.
  static bool ValidateSequence(const std::vector<CowMergeOperation>& sequence);

  // Generates a merge sequence from |operations_|, puts the result in
  // |sequence|. Returns false on failure.
  bool Generate(std::vector<CowMergeOperation>* sequence) const;

 private:
  friend class MergeSequenceGeneratorTest;
  explicit MergeSequenceGenerator(std::vector<CowMergeOperation> transfers)
      : operations_(std::move(transfers)) {}

  // For a given merge operation, finds all the operations that should merge
  // after myself. Put the result in |merge_after|.
  bool FindDependency(std::map<CowMergeOperation, std::set<CowMergeOperation>>*
                          merge_after) const;
  // The list of CowMergeOperations to sort.
  std::vector<CowMergeOperation> operations_;
};

}  // namespace chromeos_update_engine
#endif
