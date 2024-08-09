// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/partition_migrator.h"

#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/logging.h>

#include "installer/cgpt_manager.h"

namespace installer {

PartitionMigrator::PartitionMigrator(
    bool add_at_end,
    Partition reclaimed_partition,
    std::vector<Partition>&& new_partitions,
    std::vector<Partition>&& relabeled_partitions,
    std::unique_ptr<CgptManagerInterface> cgpt_manager)
    : add_at_end_(add_at_end),
      reclaimed_partition_(std::move(reclaimed_partition)),
      new_partitions_(std::move(new_partitions)),
      relabeled_partitions_(std::move(relabeled_partitions)),
      cgpt_manager_(std::move(cgpt_manager)) {}

PartitionMigrator::~PartitionMigrator() {
  std::ignore = cgpt_manager_->Finalize();
}

bool PartitionMigrator::RunMigration() {
  InitializePartitionMetadata();

  if (!ReclaimAndAddNewPartitions()) {
    LOG(ERROR) << "Failed to add new partitions";
    return false;
  }

  if (!RelabelExistingPartitions()) {
    LOG(ERROR) << "Failed to relabel existing partitions";
    return false;
  }

  return true;
}

void PartitionMigrator::RevertMigration() {
  RevertPartitionMetadata();
  UndoPartitionRelabel();
  RemoveNewPartitionsAndClaim();
}

bool PartitionMigrator::ReclaimAndAddNewPartitions() {
  // First reclaim space from the reclaimed partition.
  if (!SetSectorRange(reclaimed_partition_)) {
    LOG(ERROR) << "Failed to reclaim space from the reclaimed partition";
    return false;
  }

  for (auto& partition : new_partitions_) {
    if (!AddPartition(partition)) {
      LOG(ERROR) << "Failed to add partition " << partition.label;
      return false;
    }
  }
  return true;
}

bool PartitionMigrator::RemoveNewPartitionsAndClaim() {
  for (auto& partition : new_partitions_) {
    if (!RemovePartition(partition)) {
      LOG(ERROR) << "Failed to remove partition " << partition.label;
      return false;
    }
  }

  // First reclaim space from the reclaimed partition.
  if (!SetSectorRange(reclaimed_partition_)) {
    LOG(ERROR) << "Failed to reclaim space from the reclaimed partition";
    return false;
  }

  return true;
}

void PartitionMigrator::InitializePartitionMetadata() {
  // Calculate the reverse "shift" for the reclaimed partition.
  uint64_t reclamation_shift = 0;
  uint64_t current_sector, last_sector;
  for (auto& partition : new_partitions_) {
    reclamation_shift += partition.size;
  }
  CHECK(reclamation_shift <= reclaimed_partition_.size);

  if (add_at_end_) {
    current_sector = reclaimed_partition_.start + reclaimed_partition_.size -
                     reclamation_shift;
  } else {
    current_sector = reclaimed_partition_.start;
    reclaimed_partition_.start += reclamation_shift;
  }

  reclaimed_partition_.size -= reclamation_shift;
  last_sector = current_sector + reclamation_shift;

  for (auto& partition : new_partitions_) {
    partition.start = current_sector;
    current_sector += partition.size;
    CHECK(current_sector <= last_sector);
  }

  LOG(INFO) << "Post-calculation partition sizes:";

  reclaimed_partition_.PrettyPrint();
  for (auto& partition : new_partitions_)
    partition.PrettyPrint();
}

void PartitionMigrator::RevertPartitionMetadata() {
  // Calculate the "shift" for the reclaimed partition.
  uint64_t reclamation_shift = 0;
  for (auto& partition : new_partitions_)
    reclamation_shift += partition.size;

  // Revert changes to the reclaimed partition.
  if (!add_at_end_)
    reclaimed_partition_.start -= reclamation_shift;

  reclaimed_partition_.size += reclamation_shift;
}

bool PartitionMigrator::RelabelExistingPartitions() {
  for (auto& partition : relabeled_partitions_) {
    if (!RelabelPartition(partition)) {
      LOG(ERROR) << "Failed to relabel partition " << partition.old_label;
      return false;
    }
  }

  return true;
}

void PartitionMigrator::UndoPartitionRelabel() {
  for (auto& partition : relabeled_partitions_) {
    if (!RevertToOldLabel(partition)) {
      LOG(ERROR) << "Failed to relabel partition " << partition.label;
    }
  }
}

bool PartitionMigrator::RelabelPartition(const Partition& partition) {
  return cgpt_manager_->SetLabel(PartitionNum(partition.number),
                                 partition.label) == CgptErrorCode::kSuccess;
}

bool PartitionMigrator::RevertToOldLabel(const Partition& partition) {
  return cgpt_manager_->SetLabel(PartitionNum(partition.number),
                                 partition.old_label) ==
         CgptErrorCode::kSuccess;
}

bool PartitionMigrator::SetSectorRange(const Partition& partition) {
  return cgpt_manager_->SetSectorRange(PartitionNum(partition.number),
                                       partition.start, partition.size) ==
         CgptErrorCode::kSuccess;
}

bool PartitionMigrator::AddPartition(const Partition& partition) {
  return cgpt_manager_->AddPartition(
             PartitionNum(partition.number), partition.start, partition.size,
             partition.label, partition.type) == CgptErrorCode::kSuccess;
}

bool PartitionMigrator::RemovePartition(const Partition& partition) {
  return cgpt_manager_->AddPartition(
             PartitionNum(partition.number), partition.start, partition.size,
             partition.label, GPT_ENT_TYPE_UNUSED) == CgptErrorCode::kSuccess;
}

}  // namespace installer
