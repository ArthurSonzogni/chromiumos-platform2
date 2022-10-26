// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/error_logger.h"

#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace cros_disks {
namespace {

static_assert(!FORMAT_ERROR_NONE);
static_assert(!PARTITION_ERROR_NONE);
static_assert(!RENAME_ERROR_NONE);

template <typename T>
std::string ToString(T error) {
  std::ostringstream out;
  out << error << std::flush;
  return out.str();
}

TEST(ErrorLogger, FormatErrorType) {
  EXPECT_EQ(ToString(FORMAT_ERROR_NONE), "FORMAT_ERROR_NONE");
  EXPECT_EQ(ToString(FORMAT_ERROR_UNKNOWN), "FORMAT_ERROR_UNKNOWN");
  EXPECT_EQ(ToString(FORMAT_ERROR_INTERNAL), "FORMAT_ERROR_INTERNAL");
  EXPECT_EQ(ToString(FORMAT_ERROR_INVALID_DEVICE_PATH),
            "FORMAT_ERROR_INVALID_DEVICE_PATH");
  EXPECT_EQ(ToString(FORMAT_ERROR_DEVICE_BEING_FORMATTED),
            "FORMAT_ERROR_DEVICE_BEING_FORMATTED");
  EXPECT_EQ(ToString(FORMAT_ERROR_UNSUPPORTED_FILESYSTEM),
            "FORMAT_ERROR_UNSUPPORTED_FILESYSTEM");
  EXPECT_EQ(ToString(FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND),
            "FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND");
  EXPECT_EQ(ToString(FORMAT_ERROR_FORMAT_PROGRAM_FAILED),
            "FORMAT_ERROR_FORMAT_PROGRAM_FAILED");
  EXPECT_EQ(ToString(FORMAT_ERROR_DEVICE_NOT_ALLOWED),
            "FORMAT_ERROR_DEVICE_NOT_ALLOWED");
  EXPECT_EQ(ToString(FORMAT_ERROR_INVALID_OPTIONS),
            "FORMAT_ERROR_INVALID_OPTIONS");
  EXPECT_EQ(ToString(FORMAT_ERROR_LONG_NAME), "FORMAT_ERROR_LONG_NAME");
  EXPECT_EQ(ToString(FORMAT_ERROR_INVALID_CHARACTER),
            "FORMAT_ERROR_INVALID_CHARACTER");
  EXPECT_EQ(ToString(FormatError(987654)), "FORMAT_ERROR_987654");
}

TEST(ErrorLogger, MountErrorType) {
  EXPECT_EQ(ToString(MountError::kSuccess), "MountError::kSuccess");
  EXPECT_EQ(ToString(MountError::kUnknownError), "MountError::kUnknownError");
  EXPECT_EQ(ToString(MountError::kInternalError), "MountError::kInternalError");
  EXPECT_EQ(ToString(MountError::kInvalidArgument),
            "MountError::kInvalidArgument");
  EXPECT_EQ(ToString(MountError::kInvalidPath), "MountError::kInvalidPath");
  EXPECT_EQ(ToString(MountError::kPathAlreadyMounted),
            "MountError::kPathAlreadyMounted");
  EXPECT_EQ(ToString(MountError::kPathNotMounted),
            "MountError::kPathNotMounted");
  EXPECT_EQ(ToString(MountError::kDirectoryCreationFailed),
            "MountError::kDirectoryCreationFailed");
  EXPECT_EQ(ToString(MountError::kInvalidMountOptions),
            "MountError::kInvalidMountOptions");
  EXPECT_EQ(ToString(MountError::kInvalidUnmountOptions),
            "MountError::kInvalidUnmountOptions");
  EXPECT_EQ(ToString(MountError::kInsufficientPermissions),
            "MountError::kInsufficientPermissions");
  EXPECT_EQ(ToString(MountError::kMountProgramNotFound),
            "MountError::kMountProgramNotFound");
  EXPECT_EQ(ToString(MountError::kMountProgramFailed),
            "MountError::kMountProgramFailed");
  EXPECT_EQ(ToString(MountError::kNeedPassword), "MountError::kNeedPassword");
  EXPECT_EQ(ToString(MountError::kInProgress), "MountError::kInProgress");
  EXPECT_EQ(ToString(MountError::kCancelled), "MountError::kCancelled");
  EXPECT_EQ(ToString(MountError::kBusy), "MountError::kBusy");
  EXPECT_EQ(ToString(MountError::kInvalidDevicePath),
            "MountError::kInvalidDevicePath");
  EXPECT_EQ(ToString(MountError::kUnknownFilesystem),
            "MountError::kUnknownFilesystem");
  EXPECT_EQ(ToString(MountError::kUnsupportedFilesystem),
            "MountError::kUnsupportedFilesystem");
  EXPECT_EQ(ToString(MountError::kInvalidArchive),
            "MountError::kInvalidArchive");
  EXPECT_EQ(ToString(MountError(987654)), "MountError(987654)");
}

TEST(ErrorLogger, PartitionErrorType) {
  EXPECT_EQ(ToString(PARTITION_ERROR_NONE), "PARTITION_ERROR_NONE");
  EXPECT_EQ(ToString(PARTITION_ERROR_UNKNOWN), "PARTITION_ERROR_UNKNOWN");
  EXPECT_EQ(ToString(PARTITION_ERROR_INTERNAL), "PARTITION_ERROR_INTERNAL");
  EXPECT_EQ(ToString(PARTITION_ERROR_INVALID_DEVICE_PATH),
            "PARTITION_ERROR_INVALID_DEVICE_PATH");
  EXPECT_EQ(ToString(PARTITION_ERROR_DEVICE_BEING_PARTITIONED),
            "PARTITION_ERROR_DEVICE_BEING_PARTITIONED");
  EXPECT_EQ(ToString(PARTITION_ERROR_PROGRAM_NOT_FOUND),
            "PARTITION_ERROR_PROGRAM_NOT_FOUND");
  EXPECT_EQ(ToString(PARTITION_ERROR_PROGRAM_FAILED),
            "PARTITION_ERROR_PROGRAM_FAILED");
  EXPECT_EQ(ToString(PARTITION_ERROR_DEVICE_NOT_ALLOWED),
            "PARTITION_ERROR_DEVICE_NOT_ALLOWED");
  EXPECT_EQ(ToString(PartitionError(987654)), "PARTITION_ERROR_987654");
}

TEST(ErrorLogger, RenameErrorType) {
  EXPECT_EQ(ToString(RENAME_ERROR_NONE), "RENAME_ERROR_NONE");
  EXPECT_EQ(ToString(RENAME_ERROR_UNKNOWN), "RENAME_ERROR_UNKNOWN");
  EXPECT_EQ(ToString(RENAME_ERROR_INTERNAL), "RENAME_ERROR_INTERNAL");
  EXPECT_EQ(ToString(RENAME_ERROR_INVALID_DEVICE_PATH),
            "RENAME_ERROR_INVALID_DEVICE_PATH");
  EXPECT_EQ(ToString(RENAME_ERROR_DEVICE_BEING_RENAMED),
            "RENAME_ERROR_DEVICE_BEING_RENAMED");
  EXPECT_EQ(ToString(RENAME_ERROR_UNSUPPORTED_FILESYSTEM),
            "RENAME_ERROR_UNSUPPORTED_FILESYSTEM");
  EXPECT_EQ(ToString(RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND),
            "RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND");
  EXPECT_EQ(ToString(RENAME_ERROR_RENAME_PROGRAM_FAILED),
            "RENAME_ERROR_RENAME_PROGRAM_FAILED");
  EXPECT_EQ(ToString(RENAME_ERROR_DEVICE_NOT_ALLOWED),
            "RENAME_ERROR_DEVICE_NOT_ALLOWED");
  EXPECT_EQ(ToString(RENAME_ERROR_LONG_NAME), "RENAME_ERROR_LONG_NAME");
  EXPECT_EQ(ToString(RENAME_ERROR_INVALID_CHARACTER),
            "RENAME_ERROR_INVALID_CHARACTER");
  EXPECT_EQ(ToString(RenameError(987654)), "RENAME_ERROR_987654");
}

}  // namespace
}  // namespace cros_disks
