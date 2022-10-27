// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/error_logger.h"

#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace cros_disks {
namespace {

template <typename T>
std::string ToString(T error) {
  std::ostringstream out;
  out << error << std::flush;
  return out.str();
}

TEST(ErrorLogger, FormatErrorType) {
  EXPECT_EQ(ToString(FormatError::kSuccess), "FormatError::kSuccess");
  EXPECT_EQ(ToString(FormatError::kUnknownError), "FormatError::kUnknownError");
  EXPECT_EQ(ToString(FormatError::kInternalError),
            "FormatError::kInternalError");
  EXPECT_EQ(ToString(FormatError::kInvalidDevicePath),
            "FormatError::kInvalidDevicePath");
  EXPECT_EQ(ToString(FormatError::kDeviceBeingFormatted),
            "FormatError::kDeviceBeingFormatted");
  EXPECT_EQ(ToString(FormatError::kUnsupportedFilesystem),
            "FormatError::kUnsupportedFilesystem");
  EXPECT_EQ(ToString(FormatError::kFormatProgramNotFound),
            "FormatError::kFormatProgramNotFound");
  EXPECT_EQ(ToString(FormatError::kFormatProgramFailed),
            "FormatError::kFormatProgramFailed");
  EXPECT_EQ(ToString(FormatError::kDeviceNotAllowed),
            "FormatError::kDeviceNotAllowed");
  EXPECT_EQ(ToString(FormatError::kInvalidOptions),
            "FormatError::kInvalidOptions");
  EXPECT_EQ(ToString(FormatError::kLongName), "FormatError::kLongName");
  EXPECT_EQ(ToString(FormatError::kInvalidCharacter),
            "FormatError::kInvalidCharacter");
  EXPECT_EQ(ToString(FormatError(987654)), "FormatError(987654)");
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
  EXPECT_EQ(ToString(PartitionError::kSuccess), "PartitionError::kSuccess");
  EXPECT_EQ(ToString(PartitionError::kUnknownError),
            "PartitionError::kUnknownError");
  EXPECT_EQ(ToString(PartitionError::kInternalError),
            "PartitionError::kInternalError");
  EXPECT_EQ(ToString(PartitionError::kInvalidDevicePath),
            "PartitionError::kInvalidDevicePath");
  EXPECT_EQ(ToString(PartitionError::kDeviceBeingPartitioned),
            "PartitionError::kDeviceBeingPartitioned");
  EXPECT_EQ(ToString(PartitionError::kProgramNotFound),
            "PartitionError::kProgramNotFound");
  EXPECT_EQ(ToString(PartitionError::kProgramFailed),
            "PartitionError::kProgramFailed");
  EXPECT_EQ(ToString(PartitionError::kDeviceNotAllowed),
            "PartitionError::kDeviceNotAllowed");
  EXPECT_EQ(ToString(PartitionError(987654)), "PartitionError(987654)");
}

TEST(ErrorLogger, RenameErrorType) {
  EXPECT_EQ(ToString(RenameError::kSuccess), "RenameError::kSuccess");
  EXPECT_EQ(ToString(RenameError::kUnknownError), "RenameError::kUnknownError");
  EXPECT_EQ(ToString(RenameError::kInternalError),
            "RenameError::kInternalError");
  EXPECT_EQ(ToString(RenameError::kInvalidDevicePath),
            "RenameError::kInvalidDevicePath");
  EXPECT_EQ(ToString(RenameError::kDeviceBeingRenamed),
            "RenameError::kDeviceBeingRenamed");
  EXPECT_EQ(ToString(RenameError::kUnsupportedFilesystem),
            "RenameError::kUnsupportedFilesystem");
  EXPECT_EQ(ToString(RenameError::kRenameProgramNotFound),
            "RenameError::kRenameProgramNotFound");
  EXPECT_EQ(ToString(RenameError::kRenameProgramFailed),
            "RenameError::kRenameProgramFailed");
  EXPECT_EQ(ToString(RenameError::kDeviceNotAllowed),
            "RenameError::kDeviceNotAllowed");
  EXPECT_EQ(ToString(RenameError::kLongName), "RenameError::kLongName");
  EXPECT_EQ(ToString(RenameError::kInvalidCharacter),
            "RenameError::kInvalidCharacter");
  EXPECT_EQ(ToString(RenameError(987654)), "RenameError(987654)");
}

}  // namespace
}  // namespace cros_disks
