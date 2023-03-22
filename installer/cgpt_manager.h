// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_CGPT_MANAGER_H_
#define INSTALLER_CGPT_MANAGER_H_

#include <iostream>
#include <string>

#include <base/files/file_path.h>

#include <vboot/gpt.h>

#include "installer/inst_util.h"

// This file defines a simple C++ wrapper class interface for the cgpt methods.

// These are the possible error codes that can be returned by the CgptManager.
enum class [[nodiscard]] CgptErrorCode {
  kSuccess = 0,
  kNotInitialized = 1,
  kUnknownError = 2,
  kInvalidArgument = 3,
};

std::ostream& operator<<(std::ostream& os, const CgptErrorCode& error);

// CgptManager exposes methods to manipulate the Guid Partition Table as needed
// for ChromeOS scenarios.
class CgptManager {
 public:
  // Default constructor. The Initialize method must be called before
  // any other method can be called on this class.
  CgptManager();

  // Destructor. Automatically closes any opened device.
  ~CgptManager();

  // Opens the given device_name (e.g. "/dev/sdc") and initializes
  // with the Guid Partition Table of that device. This is the first method
  // that should be called on this class.  Otherwise those methods will
  // return kNotInitialized.
  // Returns kSuccess or an appropriate error code.
  // This device is automatically closed when this object is destructed.
  CgptErrorCode Initialize(const base::FilePath& device_name);

  // Performs any necessary write-backs so that the GPT structs are written to
  // the device. This method is called in the destructor but its error code is
  // not checked. Therefore, it is best to call Finalize yourself and check the
  // returned code.
  CgptErrorCode Finalize();

  // Sets the "successful" attribute of the given kernelPartition to 0 or 1
  // based on the value of is_successful being true (1) or false(0)
  // Returns kSuccess or an appropriate error code.
  CgptErrorCode SetSuccessful(PartitionNum partition_number,
                              bool is_successful);

  // Sets the "NumTriesLeft" attribute of the given kernelPartition to
  // the given num_tries_left value.
  // Returns kSuccess or an appropriate error code.
  CgptErrorCode SetNumTriesLeft(PartitionNum partition_number,
                                int num_tries_left);

  // Sets the "Priority" attribute of the given kernelPartition to
  // the given priority value.
  // Returns kSuccess or an appropriate error code.
  CgptErrorCode SetPriority(PartitionNum partition_number, uint8_t priority);

  // Populates the unique_id parameter with the Guid that uniquely identifies
  // the given partition_number.
  // Returns kSuccess or an appropriate error code.
  CgptErrorCode GetPartitionUniqueId(PartitionNum partition_number,
                                     Guid* unique_id) const;

  // Sets the "Priority" attribute of a partition to make it higher than all
  // other partitions. If necessary, the priorities of other partitions are
  // reduced to ensure no other partition has a higher priority.
  //
  // It preserves the relative ordering among the remaining partitions and
  // doesn't touch the partitions whose priorities are zero.
  //
  // Returns kSuccess or an appropriate error code.
  CgptErrorCode SetHighestPriority(PartitionNum partition_number);

 private:
  // The device name that is passed to Initialize.
  base::FilePath device_name_;
  // The size of that device in case we store GPT structs off site (such as on
  // NOR flash). Zero if we store GPT structs on the same device.
  uint64_t device_size_;
  bool is_initialized_;

  CgptManager(const CgptManager&);
  void operator=(const CgptManager&);
};

#endif  // INSTALLER_CGPT_MANAGER_H_
