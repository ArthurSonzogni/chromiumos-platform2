// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/cros_disks_server.h"

#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "cros-disks/archive_manager.h"
#include "cros-disks/device_event.h"
#include "cros-disks/disk.h"
#include "cros-disks/disk_manager.h"
#include "cros-disks/format_manager.h"
#include "cros-disks/platform.h"
#include "cros-disks/rename_manager.h"

using std::string;
using std::vector;

namespace cros_disks {

CrosDisksServer::CrosDisksServer(scoped_refptr<dbus::Bus> bus,
                                 Platform* platform,
                                 DiskManager* disk_manager,
                                 FormatManager* format_manager,
                                 RenameManager* rename_manager)
    : org::chromium::CrosDisksAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kCrosDisksServicePath)),
      platform_(platform),
      disk_manager_(disk_manager),
      format_manager_(format_manager),
      rename_manager_(rename_manager) {
  CHECK(platform_) << "Invalid platform object";
  CHECK(disk_manager_) << "Invalid disk manager object";
  CHECK(format_manager_) << "Invalid format manager object";
  CHECK(rename_manager_) << "Invalid rename manager object";

  format_manager_->set_observer(this);
  rename_manager_->set_observer(this);
}

void CrosDisksServer::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void CrosDisksServer::RegisterMountManager(MountManager* mount_manager) {
  CHECK(mount_manager) << "Invalid mount manager object";
  mount_managers_.push_back(mount_manager);
}

void CrosDisksServer::Format(const string& path,
                             const string& filesystem_type,
                             const vector<string>& options) {
  FormatErrorType error_type = FORMAT_ERROR_NONE;
  Disk disk;
  if (!disk_manager_->GetDiskByDevicePath(path, &disk)) {
    error_type = FORMAT_ERROR_INVALID_DEVICE_PATH;
  } else if (disk.is_on_boot_device) {
    error_type = FORMAT_ERROR_DEVICE_NOT_ALLOWED;
  } else {
    error_type = format_manager_->StartFormatting(path, disk.device_file,
                                                  filesystem_type);
  }

  if (error_type != FORMAT_ERROR_NONE) {
    LOG(ERROR) << "Could not format device '" << path << "' as filesystem '"
               << filesystem_type << "'";
    SendFormatCompletedSignal(error_type, path);
  }
}

void CrosDisksServer::Rename(const string& path, const string& volume_name) {
  RenameErrorType error_type = RENAME_ERROR_NONE;
  Disk disk;
  if (!disk_manager_->GetDiskByDevicePath(path, &disk)) {
    error_type = RENAME_ERROR_INVALID_DEVICE_PATH;
  } else if (disk.is_on_boot_device || disk.is_read_only) {
    error_type = RENAME_ERROR_DEVICE_NOT_ALLOWED;
  } else {
    error_type = rename_manager_->StartRenaming(
        path, disk.device_file, volume_name, disk.filesystem_type);
  }

  if (error_type != RENAME_ERROR_NONE) {
    LOG(ERROR) << "Could not rename device '" << path << "' as '" << volume_name
               << "'";
    SendRenameCompletedSignal(error_type, path);
  }
}

MountManager* CrosDisksServer::FindMounter(const string& source_path) const {
  for (const auto& manager : mount_managers_) {
    if (manager->CanMount(source_path)) {
      return manager;
    }
  }
  return nullptr;
}

void CrosDisksServer::Mount(const string& path,
                            const string& filesystem_type,
                            const vector<string>& options) {
  MountErrorType error_type = MOUNT_ERROR_INVALID_PATH;
  MountSourceType source_type = MOUNT_SOURCE_INVALID;
  string source_path;
  string mount_path;

  if (platform_->GetRealPath(path, &source_path)) {
    MountManager* mounter = FindMounter(source_path);
    if (mounter) {
      source_type = mounter->GetMountSourceType();
      error_type =
          mounter->Mount(source_path, filesystem_type, options, &mount_path);
    }
  }

  if (error_type != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to mount '" << path << "'";
  }
  SendMountCompletedSignal(error_type, path, source_type, mount_path);
}

bool CrosDisksServer::Unmount(brillo::ErrorPtr* error,
                              const string& path,
                              const vector<string>& options) {
  MountErrorType error_type = MOUNT_ERROR_INVALID_PATH;
  for (const auto& manager : mount_managers_) {
    if (manager->CanUnmount(path)) {
      error_type = manager->Unmount(path, options);
      break;
    }
  }

  if (error_type == MOUNT_ERROR_NONE) {
    return true;
  }

  string message = "Failed to unmount '" + path + "'";
  brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                       kCrosDisksServiceError, message);
  return false;
}

void CrosDisksServer::UnmountAll() {
  for (const auto& manager : mount_managers_) {
    manager->UnmountAll();
  }
}

vector<string> CrosDisksServer::DoEnumerateDevices(
    bool auto_mountable_only) const {
  vector<Disk> disks = disk_manager_->EnumerateDisks();
  vector<string> devices;
  devices.reserve(disks.size());
  for (const auto& disk : disks) {
    if (!auto_mountable_only || disk.is_auto_mountable) {
      devices.push_back(disk.native_path);
    }
  }
  return devices;
}

vector<string> CrosDisksServer::EnumerateDevices() {
  return DoEnumerateDevices(false);
}

vector<string> CrosDisksServer::EnumerateAutoMountableDevices() {
  return DoEnumerateDevices(true);
}

vector<CrosDisksServer::DBusMountEntry>
CrosDisksServer::EnumerateMountEntries() {
  vector<DBusMountEntry> dbus_mount_entries;
  for (const auto& manager : mount_managers_) {
    for (const auto& mount_entry : manager->GetMountEntries()) {
      dbus_mount_entries.push_back(
          std::make_tuple(static_cast<uint32_t>(mount_entry.error_type),
                          mount_entry.source_path,
                          static_cast<uint32_t>(mount_entry.source_type),
                          mount_entry.mount_path));
    }
  }
  return dbus_mount_entries;
}

bool CrosDisksServer::GetDeviceProperties(
    brillo::ErrorPtr* error,
    const string& device_path,
    brillo::VariantDictionary* properties) {
  Disk disk;
  if (!disk_manager_->GetDiskByDevicePath(device_path, &disk)) {
    string message = "Could not get the properties of device " + device_path;
    LOG(ERROR) << message;
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kCrosDisksServiceError, message);
    return false;
  }

  brillo::VariantDictionary temp_properties;
  temp_properties[kDeviceIsDrive] = disk.is_drive;
  temp_properties[kDevicePresentationHide] = disk.is_hidden;
  temp_properties[kDeviceIsMounted] = disk.IsMounted();
  temp_properties[kDeviceIsMediaAvailable] = disk.is_media_available;
  temp_properties[kDeviceIsOnBootDevice] = disk.is_on_boot_device;
  temp_properties[kDeviceIsOnRemovableDevice] = disk.is_on_removable_device;
  temp_properties[kDeviceIsVirtual] = disk.is_virtual;
  temp_properties[kNativePath] = disk.native_path;
  temp_properties[kDeviceFile] = disk.device_file;
  temp_properties[kIdUuid] = disk.uuid;
  temp_properties[kIdLabel] = disk.label;
  temp_properties[kVendorId] = disk.vendor_id;
  temp_properties[kVendorName] = disk.vendor_name;
  temp_properties[kProductId] = disk.product_id;
  temp_properties[kProductName] = disk.product_name;
  temp_properties[kDriveModel] = disk.drive_model;
  temp_properties[kDriveIsRotational] = disk.is_rotational;
  temp_properties[kDeviceMediaType] = static_cast<uint32_t>(disk.media_type);
  temp_properties[kDeviceSize] = disk.device_capacity;
  temp_properties[kDeviceIsReadOnly] = disk.is_read_only;
  temp_properties[kFileSystemType] = disk.filesystem_type;
  temp_properties[kDeviceMountPaths] = disk.mount_paths;
  *properties = std::move(temp_properties);
  return true;
}

void CrosDisksServer::OnFormatCompleted(const string& device_path,
                                        FormatErrorType error_type) {
  SendFormatCompletedSignal(error_type, device_path);
}

void CrosDisksServer::OnRenameCompleted(const string& device_path,
                                        RenameErrorType error_type) {
  SendRenameCompletedSignal(error_type, device_path);
}

void CrosDisksServer::OnScreenIsLocked() {
  // no-op
}

void CrosDisksServer::OnScreenIsUnlocked() {
  // no-op
}

void CrosDisksServer::OnSessionStarted() {
  for (const auto& manager : mount_managers_) {
    manager->StartSession();
  }
}

void CrosDisksServer::OnSessionStopped() {
  for (const auto& manager : mount_managers_) {
    manager->StopSession();
  }
}

void CrosDisksServer::DispatchDeviceEvent(const DeviceEvent& event) {
  switch (event.event_type) {
    case DeviceEvent::kDeviceAdded:
      SendDeviceAddedSignal(event.device_path);
      break;
    case DeviceEvent::kDeviceScanned:
      SendDeviceScannedSignal(event.device_path);
      break;
    case DeviceEvent::kDeviceRemoved:
      SendDeviceRemovedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskAdded:
      SendDiskAddedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskChanged:
      SendDiskChangedSignal(event.device_path);
      break;
    case DeviceEvent::kDiskRemoved:
      SendDiskRemovedSignal(event.device_path);
      break;
    default:
      break;
  }
}

}  // namespace cros_disks
