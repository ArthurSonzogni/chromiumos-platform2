// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_PARTITION_MIGRATOR_H_
#define INSTALLER_PARTITION_MIGRATOR_H_

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <installer/cgpt_manager.h>
#include <vboot/cgpt_params.h>
#include <vboot/gpt.h>
#include <vboot/vboot_host.h>

namespace installer {

// Represents partition on a block device.
struct Partition {
  int number = 0;
  std::string label;
  std::string old_label;
  uint64_t start = 0;
  uint64_t size = 0;
  Guid type;

  void PrettyPrint() {
    char guid_str[GUID_STRLEN];
    GuidToStr(&type, guid_str, GUID_STRLEN);

    LOG(INFO) << "Partition " << number;
    LOG(INFO) << "  Label: " << label;
    LOG(INFO) << "  Old Label: " << old_label;
    LOG(INFO) << "  Start: " << start;
    LOG(INFO) << "  Size: " << size;
    LOG(INFO) << "  Type: " << guid_str;
  }
};

class PartitionMigrator {
 public:
  PartitionMigrator(bool add_at_end,
                    Partition reclaimed_partition,
                    std::vector<Partition>&& new_partitions,
                    std::vector<Partition>&& relabeled_partitions,
                    std::unique_ptr<CgptManagerInterface> cgpt_manager);
  PartitionMigrator(const PartitionMigrator&) = delete;
  PartitionMigrator& operator=(const PartitionMigrator&) = delete;

  ~PartitionMigrator();

  bool RunMigration();
  void RevertMigration();

  void InitializePartitionMetadata();
  void RevertPartitionMetadata();

  Partition get_reclaimed_partition_for_test() const {
    return reclaimed_partition_;
  }

  std::vector<Partition> get_new_partitions_for_test() const {
    return new_partitions_;
  }

 private:
  bool RelabelPartition(const Partition& partition);
  bool RevertToOldLabel(const Partition& partition);
  bool SetSectorRange(const Partition& partition);
  bool AddPartition(const Partition& partition);
  bool RemovePartition(const Partition& partition);

  bool ReclaimAndAddNewPartitions();
  bool RelabelExistingPartitions();

  // Revert path.
  bool RemoveNewPartitionsAndClaim();
  void UndoPartitionRelabel();

  bool add_at_end_;

  Partition reclaimed_partition_;
  std::vector<Partition> new_partitions_;

  const std::vector<Partition> relabeled_partitions_;
  std::unique_ptr<CgptManagerInterface> cgpt_manager_;
};

}  // namespace installer

#endif  // INSTALLER_PARTITION_MIGRATOR_H_
