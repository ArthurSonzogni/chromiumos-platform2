// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/shared_data.h"

#include <optional>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "vm_tools/common/naming.h"

namespace vm_tools {
namespace concierge {

std::optional<base::FilePath> GetFilePathFromName(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    StorageLocation storage_location,
    const std::string& extension,
    bool create_parent_dir) {
  if (!IsValidOwnerId(cryptohome_id)) {
    LOG(ERROR) << "Invalid cryptohome_id specified";
    return std::nullopt;
  }
  // Encode the given disk name to ensure it only has valid characters.
  std::string encoded_name = GetEncodedName(vm_name);

  base::FilePath storage_dir = base::FilePath(kCryptohomeRoot);
  switch (storage_location) {
    case STORAGE_CRYPTOHOME_ROOT: {
      storage_dir = storage_dir.Append(kCrosvmDir);
      break;
    }
    case STORAGE_CRYPTOHOME_PLUGINVM: {
      storage_dir = storage_dir.Append(kPluginVmDir);
      break;
    }
    default: {
      LOG(ERROR) << "Unknown storage location type";
      return std::nullopt;
    }
  }
  storage_dir = storage_dir.Append(cryptohome_id);

  if (!base::DirectoryExists(storage_dir)) {
    if (!create_parent_dir) {
      return std::nullopt;
    }
    base::File::Error dir_error;

    if (!base::CreateDirectoryAndGetError(storage_dir, &dir_error)) {
      LOG(ERROR) << "Failed to create storage directory " << storage_dir << ": "
                 << base::File::ErrorToString(dir_error);
      return std::nullopt;
    }
  }

  if (base::IsLink(storage_dir)) {
    LOG(ERROR) << "Invalid symlinked storage directory " << storage_dir;
    return std::nullopt;
  }

  // Group rx permission needed for VM shader cache management by shadercached
  if (!base::SetPosixFilePermissions(storage_dir, 0750)) {
    LOG(WARNING) << "Failed to set directory permissions for " << storage_dir;
  }
  return storage_dir.Append(encoded_name).AddExtension(extension);
}

bool GetPluginDirectory(const base::FilePath& prefix,
                        const std::string& extension,
                        const std::string& vm_id,
                        bool create,
                        base::FilePath* path_out) {
  std::string dirname = GetEncodedName(vm_id);

  base::FilePath path = prefix.Append(dirname).AddExtension(extension);
  if (create && !base::DirectoryExists(path)) {
    base::File::Error dir_error;
    if (!base::CreateDirectoryAndGetError(path, &dir_error)) {
      LOG(ERROR) << "Failed to create plugin directory " << path.value() << ": "
                 << base::File::ErrorToString(dir_error);
      return false;
    }
  }

  *path_out = path;
  return true;
}

bool GetPluginIsoDirectory(const std::string& vm_id,
                           const std::string& cryptohome_id,
                           bool create,
                           base::FilePath* path_out) {
  return GetPluginDirectory(base::FilePath(kCryptohomeRoot)
                                .Append(kPluginVmDir)
                                .Append(cryptohome_id),
                            "iso", vm_id, create, path_out);
}

// Valid owner/cryptohome ID is a hexadecimal string.
bool IsValidOwnerId(const std::string& owner_id) {
  if (owner_id.empty())
    return false;

  return base::ContainsOnlyChars(owner_id, kValidCryptoHomeCharacters);
}

// Currently the only requirement for VM name to be non-empty because we
// encode them as base64 when creating on-disk representations.
bool IsValidVmName(const std::string& vm_name) {
  return !vm_name.empty();
}

void SendDbusResponse(dbus::ExportedObject::ResponseSender response_sender,
                      dbus::MethodCall* method_call,
                      const vm_tools::concierge::StartVmResponse& response) {
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageWriter writer(dbus_response.get());
  writer.AppendProtoAsArrayOfBytes(response);
  std::move(response_sender).Run(std::move(dbus_response));
}

}  // namespace concierge
}  // namespace vm_tools
