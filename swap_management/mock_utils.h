// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_MOCK_UTILS_H_
#define SWAP_MANAGEMENT_MOCK_UTILS_H_

#include "swap_management/utils.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>

namespace swap_management {

class MockUtils : public swap_management::Utils {
 public:
  MockUtils() = default;
  MockUtils& operator=(const MockUtils&) = delete;
  MockUtils(const MockUtils&) = delete;

  MOCK_METHOD(absl::Status,
              RunProcessHelper,
              (const std::vector<std::string>& commands),
              (override));
  MOCK_METHOD(absl::Status,
              RunProcessHelper,
              (const std::vector<std::string>& commands, std::string* output),
              (override));
  MOCK_METHOD(absl::Status,
              WriteFile,
              (const base::FilePath& path, const std::string& data),
              (override));
  MOCK_METHOD(absl::Status,
              ReadFileToStringWithMaxSize,
              (const base::FilePath& path,
               std::string* contents,
               size_t max_size),
              (override));
  MOCK_METHOD(absl::Status,
              ReadFileToString,
              (const base::FilePath& path, std::string* contents),
              (override));
  MOCK_METHOD(absl::Status,
              DeleteFile,
              (const base::FilePath& path),
              (override));
  MOCK_METHOD(absl::Status,
              PathExists,
              (const base::FilePath& path),
              (override));
  MOCK_METHOD(absl::Status,
              Fallocate,
              (const base::FilePath& path, size_t size),
              (override));
  MOCK_METHOD(absl::Status,
              CreateDirectory,
              (const base::FilePath& path),
              (override));
  MOCK_METHOD(absl::Status,
              SetPosixFilePermissions,
              (const base::FilePath& path, int mode),
              (override));
  MOCK_METHOD(absl::Status,
              Mount,
              (const std::string& source,
               const std::string& target,
               const std::string& fs_type,
               uint64_t mount_flags,
               const std::string& data),
              (override));
  MOCK_METHOD(absl::Status, Umount, (const std::string& target), (override));
  MOCK_METHOD(absl::StatusOr<struct statfs>,
              GetStatfs,
              (const std::string& path),
              (override));
  MOCK_METHOD(absl::StatusOr<std::string>,
              GenerateRandHex,
              (size_t size),
              (override));
  MOCK_METHOD(absl::StatusOr<base::SystemMemoryInfoKB>,
              GetSystemMemoryInfo,
              (),
              (override));
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_MOCK_UTILS_H_
