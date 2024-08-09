// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_MOCK_CGPT_MANAGER_H_
#define INSTALLER_MOCK_CGPT_MANAGER_H_

#include <string>

#include <gmock/gmock.h>

#include "installer/cgpt_manager.h"

class MockCgptManager : public CgptManagerInterface {
 public:
  MOCK_METHOD(CgptErrorCode,
              Initialize,
              (const base::FilePath& device_name),
              (override));
  MOCK_METHOD(CgptErrorCode, Finalize, (), (override));
  MOCK_METHOD(CgptErrorCode,
              SetSuccessful,
              (PartitionNum partition_number, bool is_successful),
              (override));
  MOCK_METHOD(CgptErrorCode,
              SetNumTriesLeft,
              (PartitionNum partition_number, int num_tries_left),
              (override));
  MOCK_METHOD(CgptErrorCode,
              SetPriority,
              (PartitionNum partition_number, uint8_t priority),
              (override));
  MOCK_METHOD(CgptErrorCode,
              GetPartitionUniqueId,
              (PartitionNum partition_number, Guid* unique_id),
              (const, override));
  MOCK_METHOD(CgptErrorCode,
              SetHighestPriority,
              (PartitionNum partition_number),
              (override));
  MOCK_METHOD(CgptErrorCode,
              GetSectorRange,
              (PartitionNum partition_number, SectorRange& sectors),
              (const override));
  MOCK_METHOD(CgptErrorCode,
              SetSectorRange,
              (PartitionNum partition_number,
               std::optional<uint64_t> start,
               std::optional<uint64_t> count),
              (override));
  MOCK_METHOD(CgptErrorCode, RepairPartitionTable, (), (override));
  MOCK_METHOD(const base::FilePath&, DeviceName, (), (const override));
  MOCK_METHOD(CgptErrorCode,
              SetLabel,
              (PartitionNum partition_number, const std::string& new_label),
              (override));
  MOCK_METHOD(CgptErrorCode,
              AddPartition,
              (PartitionNum partition_number,
               uint64_t start,
               uint64_t size,
               const std::string& label,
               Guid type),
              (override));
};

#endif  // INSTALLER_MOCK_CGPT_MANAGER_H_
