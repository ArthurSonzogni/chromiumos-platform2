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
    CROS_DISKS_PRINT(FORMAT_ERROR_NONE)
    CROS_DISKS_PRINT(FORMAT_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(FORMAT_ERROR_INTERNAL)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(FORMAT_ERROR_DEVICE_BEING_FORMATTED)
    CROS_DISKS_PRINT(FORMAT_ERROR_UNSUPPORTED_FILESYSTEM)
    CROS_DISKS_PRINT(FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(FORMAT_ERROR_FORMAT_PROGRAM_FAILED)
    CROS_DISKS_PRINT(FORMAT_ERROR_DEVICE_NOT_ALLOWED)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_OPTIONS)
    CROS_DISKS_PRINT(FORMAT_ERROR_LONG_NAME)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_CHARACTER)
  }
  return out << "FORMAT_ERROR_"
             << static_cast<std::underlying_type_t<FormatError>>(error);
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
    CROS_DISKS_PRINT(PARTITION_ERROR_NONE)
    CROS_DISKS_PRINT(PARTITION_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(PARTITION_ERROR_INTERNAL)
    CROS_DISKS_PRINT(PARTITION_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(PARTITION_ERROR_DEVICE_BEING_PARTITIONED)
    CROS_DISKS_PRINT(PARTITION_ERROR_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(PARTITION_ERROR_PROGRAM_FAILED)
    CROS_DISKS_PRINT(PARTITION_ERROR_DEVICE_NOT_ALLOWED)
  }
  return out << "PARTITION_ERROR_"
             << static_cast<std::underlying_type_t<PartitionError>>(error);
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
