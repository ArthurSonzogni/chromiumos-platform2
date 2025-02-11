// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/cgpt_manager.h"

#include <linux/major.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/stringprintf.h>

#include "installer/inst_util.h"

extern "C" {
#include <vboot/vboot_host.h>
}

using std::string;

// This file implements the C++ wrapper methods over the C cgpt methods.

std::ostream& operator<<(std::ostream& os, const CgptErrorCode& error) {
  switch (error) {
    case CgptErrorCode::kSuccess:
      os << "CgptErrorCode::kSuccess";
      break;
    case CgptErrorCode::kUnknownError:
      os << "CgptErrorCode::kUnknownError";
      break;
    case CgptErrorCode::kInvalidArgument:
      os << "CgptErrorCode::kInvalidArgument";
      break;
  }
  return os;
}

CgptManager::CgptManager(const base::FilePath& device_name)
    : device_name_(device_name) {}
CgptErrorCode CgptManager::SetSuccessful(PartitionNum partition_number,
                                         bool is_successful) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  params.successful = is_successful;
  params.set_successful = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::SetNumTriesLeft(PartitionNum partition_number,
                                           int numTries) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  params.tries = numTries;
  params.set_tries = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::SetPriority(PartitionNum partition_number,
                                       uint8_t priority) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  params.priority = priority;
  params.set_priority = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::GetPartitionUniqueId(PartitionNum partition_number,
                                                Guid* unique_id) const {
  if (!unique_id) {
    return CgptErrorCode::kInvalidArgument;
  }

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  *unique_id = params.unique_guid;
  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::SetHighestPriority(PartitionNum partition_number) {
  CgptPrioritizeParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.set_partition = partition_number.Value();
  // The internal implementation in CgptPrioritize automatically computes the
  // right priority number if we supply 0 for the max_priority argument.
  params.max_priority = 0;

  int retval = CgptPrioritize(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::GetSectorRange(PartitionNum partition_number,
                                          SectorRange& sectors) const {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));
  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  sectors.start = params.begin;
  sectors.count = params.size;
  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::SetSectorRange(PartitionNum partition_number,
                                          std::optional<uint64_t> start,
                                          std::optional<uint64_t> count) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  // At least one of the inputs must have a value.
  if (!start.has_value() && !count.has_value()) {
    return CgptErrorCode::kInvalidArgument;
  }

  if (start.has_value()) {
    params.begin = start.value();
    params.set_begin = true;
  }
  if (count.has_value()) {
    params.size = count.value();
    params.set_size = true;
  }

  int retval = CgptAdd(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::SetLabel(PartitionNum partition_number,
                                    const std::string& label) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  params.label = label.c_str();

  int retval = CgptAdd(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::AddPartition(PartitionNum partition_number,
                                        uint64_t start,
                                        uint64_t size,
                                        const std::string& label,
                                        Guid type) {
  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  params.partition = partition_number.Value();

  params.label = label.c_str();
  params.begin = start;
  params.size = size;
  params.type_guid = type;

  // GenerateUuid() is stubbed in the libvboot_host to remove dependency
  // libuuid.
  base::RandBytes(base::as_writable_byte_span(params.unique_guid.u.raw));

  params.set_begin = 1;
  params.set_size = 1;
  params.set_type = 1;
  params.set_unique = 1;

  int retval = CgptAdd(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

CgptErrorCode CgptManager::RepairPartitionTable() {
  CgptRepairParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char*>(device_name_.value().c_str());
  // This prints the result of the validity check.
  params.verbose = true;

  int retval = CgptRepair(&params);
  if (retval != CGPT_OK) {
    return CgptErrorCode::kUnknownError;
  }

  return CgptErrorCode::kSuccess;
}

const base::FilePath& CgptManager::DeviceName() const {
  return device_name_;
}
