// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/error_logger.h"

#include <type_traits>

namespace cros_disks {

#define CROS_DISKS_PRINT(X) \
  case X:                   \
    return out << #X;

std::ostream& operator<<(std::ostream& out, const FormatError error) {
  switch (error) {
    CROS_DISKS_PRINT(FormatError::kSuccess)
    CROS_DISKS_PRINT(FormatError::kUnknownError)
    CROS_DISKS_PRINT(FormatError::kInternalError)
    CROS_DISKS_PRINT(FormatError::kInvalidDevicePath)
    CROS_DISKS_PRINT(FormatError::kDeviceBeingFormatted)
    CROS_DISKS_PRINT(FormatError::kUnsupportedFilesystem)
    CROS_DISKS_PRINT(FormatError::kFormatProgramNotFound)
    CROS_DISKS_PRINT(FormatError::kFormatProgramFailed)
    CROS_DISKS_PRINT(FormatError::kDeviceNotAllowed)
    CROS_DISKS_PRINT(FormatError::kInvalidOptions)
    CROS_DISKS_PRINT(FormatError::kLongName)
    CROS_DISKS_PRINT(FormatError::kInvalidCharacter)
  }
  return out << "FormatError("
             << static_cast<std::underlying_type_t<FormatError>>(error) << ")";
}

std::ostream& operator<<(std::ostream& out, const MountError error) {
  switch (error) {
    CROS_DISKS_PRINT(MountError::kSuccess)
    CROS_DISKS_PRINT(MountError::kUnknownError)
    CROS_DISKS_PRINT(MountError::kInternalError)
    CROS_DISKS_PRINT(MountError::kInvalidArgument)
    CROS_DISKS_PRINT(MountError::kInvalidPath)
    CROS_DISKS_PRINT(MountError::kPathAlreadyMounted)
    CROS_DISKS_PRINT(MountError::kPathNotMounted)
    CROS_DISKS_PRINT(MountError::kDirectoryCreationFailed)
    CROS_DISKS_PRINT(MountError::kInvalidMountOptions)
    CROS_DISKS_PRINT(MountError::kInvalidUnmountOptions)
    CROS_DISKS_PRINT(MountError::kInsufficientPermissions)
    CROS_DISKS_PRINT(MountError::kMountProgramNotFound)
    CROS_DISKS_PRINT(MountError::kMountProgramFailed)
    CROS_DISKS_PRINT(MountError::kNeedPassword)
    CROS_DISKS_PRINT(MountError::kInProgress)
    CROS_DISKS_PRINT(MountError::kCancelled)
    CROS_DISKS_PRINT(MountError::kBusy)
    CROS_DISKS_PRINT(MountError::kInvalidDevicePath)
    CROS_DISKS_PRINT(MountError::kUnknownFilesystem)
    CROS_DISKS_PRINT(MountError::kUnsupportedFilesystem)
    CROS_DISKS_PRINT(MountError::kInvalidArchive)
  }
  return out << "MountError("
             << static_cast<std::underlying_type_t<MountError>>(error) << ")";
}

std::ostream& operator<<(std::ostream& out, const PartitionError error) {
  switch (error) {
    CROS_DISKS_PRINT(PartitionError::kSuccess)
    CROS_DISKS_PRINT(PartitionError::kUnknownError)
    CROS_DISKS_PRINT(PartitionError::kInternalError)
    CROS_DISKS_PRINT(PartitionError::kInvalidDevicePath)
    CROS_DISKS_PRINT(PartitionError::kDeviceBeingPartitioned)
    CROS_DISKS_PRINT(PartitionError::kProgramNotFound)
    CROS_DISKS_PRINT(PartitionError::kProgramFailed)
    CROS_DISKS_PRINT(PartitionError::kDeviceNotAllowed)
  }
  return out << "PartitionError("
             << static_cast<std::underlying_type_t<PartitionError>>(error)
             << ")";
}

std::ostream& operator<<(std::ostream& out, const RenameError error) {
  switch (error) {
    CROS_DISKS_PRINT(RenameError::kSuccess)
    CROS_DISKS_PRINT(RenameError::kUnknownError)
    CROS_DISKS_PRINT(RenameError::kInternalError)
    CROS_DISKS_PRINT(RenameError::kInvalidDevicePath)
    CROS_DISKS_PRINT(RenameError::kDeviceBeingRenamed)
    CROS_DISKS_PRINT(RenameError::kUnsupportedFilesystem)
    CROS_DISKS_PRINT(RenameError::kRenameProgramNotFound)
    CROS_DISKS_PRINT(RenameError::kRenameProgramFailed)
    CROS_DISKS_PRINT(RenameError::kDeviceNotAllowed)
    CROS_DISKS_PRINT(RenameError::kLongName)
    CROS_DISKS_PRINT(RenameError::kInvalidCharacter)
  }
  return out << "RenameError("
             << static_cast<std::underlying_type_t<RenameError>>(error) << ")";
}

}  // namespace cros_disks
