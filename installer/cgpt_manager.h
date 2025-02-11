// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_CGPT_MANAGER_H_
#define INSTALLER_CGPT_MANAGER_H_

#include <iostream>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <vboot/gpt.h>

#include "installer/inst_util.h"

// This file defines a simple C++ wrapper class interface for the cgpt methods.

// These are the possible error codes that can be returned by the CgptManager.
enum class [[nodiscard]] CgptErrorCode {
  kSuccess = 0,
  kUnknownError = 1,
  kInvalidArgument = 2,
};

BRILLO_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const CgptErrorCode& error);

// Range of sectors on disk.
struct SectorRange {
  // First sector.
  uint64_t start = 0;

  // Number of sectors.
  uint64_t count = 0;
};

// CgptManagerInterface provices methods to manipulate the Guid
// Partition Table as needed for ChromeOS scenarios.
//
// A concrete implementation is provided by `CgptManager`, and a mock
// for unit tests is provided in `mock_cgpt_manager.h`.
class CgptManagerInterface {
 public:
  CgptManagerInterface() = default;

  virtual ~CgptManagerInterface() = default;

  // Sets the "successful" attribute of the given kernelPartition to 0 or 1
  // based on the value of is_successful being true (1) or false(0)
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode SetSuccessful(PartitionNum partition_number,
                                      bool is_successful) = 0;

  // Sets the "NumTriesLeft" attribute of the given kernelPartition to
  // the given num_tries_left value.
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode SetNumTriesLeft(PartitionNum partition_number,
                                        int num_tries_left) = 0;

  // Sets the "Priority" attribute of the given kernelPartition to
  // the given priority value.
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode SetPriority(PartitionNum partition_number,
                                    uint8_t priority) = 0;

  // Populates the unique_id parameter with the Guid that uniquely identifies
  // the given partition_number.
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode GetPartitionUniqueId(PartitionNum partition_number,
                                             Guid* unique_id) const = 0;

  // Sets the "Priority" attribute of a partition to make it higher than all
  // other partitions. If necessary, the priorities of other partitions are
  // reduced to ensure no other partition has a higher priority.
  //
  // It preserves the relative ordering among the remaining partitions and
  // doesn't touch the partitions whose priorities are zero.
  //
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode SetHighestPriority(PartitionNum partition_number) = 0;

  // Get the sectors used by the partition.
  // Returns kCgptSuccess or an appropriate error code.
  virtual CgptErrorCode GetSectorRange(PartitionNum partition_number,
                                       SectorRange& sectors) const = 0;

  // Set the sectors used by the partition. If |start| or |count| is
  // |std::nullopt|, the corresponding partition value will not be
  // updated. At least one of them must be set.
  // Returns kCgptSuccess or an appropriate error code.
  virtual CgptErrorCode SetSectorRange(PartitionNum partition_number,
                                       std::optional<uint64_t> start,
                                       std::optional<uint64_t> count) = 0;

  // Set the label for a partition.
  virtual CgptErrorCode SetLabel(PartitionNum partition,
                                 const std::string& new_label) = 0;

  // Add a new partition.
  virtual CgptErrorCode AddPartition(PartitionNum partition_number,
                                     uint64_t start,
                                     uint64_t size,
                                     const std::string& label,
                                     Guid type) = 0;

  // In some circumstances devices will have a damaged GPT  (at least
  // b/257478857, possibly other cases). This tries to fix it.
  //
  // Returns kSuccess or an appropriate error code.
  virtual CgptErrorCode RepairPartitionTable() = 0;

  // Get the device path (e.g. "/dev/sda") that was passed in to the
  // constructor.
  virtual const base::FilePath& DeviceName() const = 0;
};

class BRILLO_EXPORT CgptManager : public CgptManagerInterface {
 public:
  explicit CgptManager(const base::FilePath& device_name);
  CgptManager(CgptManager const&) = delete;
  CgptManager& operator=(CgptManager const&) = delete;

  CgptErrorCode SetSuccessful(PartitionNum partition_number,
                              bool is_successful) override;
  CgptErrorCode SetNumTriesLeft(PartitionNum partition_number,
                                int num_tries_left) override;
  CgptErrorCode SetPriority(PartitionNum partition_number,
                            uint8_t priority) override;
  CgptErrorCode GetPartitionUniqueId(PartitionNum partition_number,
                                     Guid* unique_id) const override;
  CgptErrorCode SetHighestPriority(PartitionNum partition_number) override;
  CgptErrorCode GetSectorRange(PartitionNum partition_number,
                               SectorRange& sectors) const override;
  CgptErrorCode SetSectorRange(PartitionNum partition_number,
                               std::optional<uint64_t> start,
                               std::optional<uint64_t> count) override;
  // Set the label for a partition.
  CgptErrorCode SetLabel(PartitionNum partition,
                         const std::string& new_label) override;

  // Add a new partition.
  CgptErrorCode AddPartition(PartitionNum partition_number,
                             uint64_t start,
                             uint64_t size,
                             const std::string& label,
                             Guid type) override;

  CgptErrorCode RepairPartitionTable() override;
  const base::FilePath& DeviceName() const override;

 private:
  // The device name that is passed to the constructor.
  base::FilePath device_name_;
};

#endif  // INSTALLER_CGPT_MANAGER_H_
