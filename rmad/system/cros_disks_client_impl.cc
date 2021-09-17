// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/cros_disks_client_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/cros-disks/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace {

void OnSignalConnected(const std::string& interface,
                       const std::string& signal,
                       bool succeeded) {
  if (!succeeded) {
    LOG(ERROR) << "Failed to connect to " << interface << " " << signal;
  } else {
    VLOG(1) << "Connected to " << interface << " " << signal;
  }
}

void OnMountCompleted(
    base::RepeatingCallback<void(const rmad::MountEntry&)> callback,
    dbus::Signal* signal) {
  VLOG(1) << "Get signal MountCompleted";
  dbus::MessageReader reader(signal);
  uint32_t error_type;
  std::string source;
  uint32_t source_type;
  std::string mount_path;
  rmad::MountEntry entry;
  if (reader.PopUint32(&error_type) && reader.PopString(&source) &&
      reader.PopUint32(&source_type) && reader.PopString(&mount_path)) {
    VLOG(1) << "Mount succeeds";
    entry.success = (error_type == cros_disks::MOUNT_ERROR_NONE);
    entry.source = source;
    entry.mount_path = mount_path;
  } else {
    VLOG(1) << "Mount failed";
    entry.success = false;
  }
  std::move(callback).Run(entry);
}

}  // namespace

namespace rmad {

CrosDisksClientImpl::CrosDisksClientImpl(const scoped_refptr<dbus::Bus>& bus) {
  proxy_ =
      bus->GetObjectProxy(cros_disks::kCrosDisksServiceName,
                          dbus::ObjectPath(cros_disks::kCrosDisksServicePath));
}

bool CrosDisksClientImpl::EnumerateDevices(std::vector<std::string>* devices) {
  CHECK(devices);

  dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                               cros_disks::kEnumerateDevices);
  std::unique_ptr<dbus::Response> response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response.get()) {
    LOG(ERROR) << "Failed to call cros-disks D-Bus service";
    return false;
  }

  dbus::MessageReader reader(response.get());
  if (!reader.PopArrayOfStrings(devices)) {
    LOG(ERROR) << "Failed to decode cros-disks response";
    return false;
  }
  return true;
}

bool CrosDisksClientImpl::GetDeviceProperties(
    const std::string& device, DeviceProperties* device_properties) {
  CHECK(device_properties);

  dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                               cros_disks::kGetDeviceProperties);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(device);
  std::unique_ptr<dbus::Response> response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response.get()) {
    LOG(ERROR) << "Failed to call cros-disks D-Bus service";
    return false;
  }

  dbus::MessageReader reader(response.get());
  brillo::VariantDictionary properties;
  if (!brillo::dbus_utils::PopValueFromReader(&reader, &properties)) {
    LOG(ERROR) << "Failed to decode cros-disks response";
    return false;
  }

  device_properties->device_file =
      properties[cros_disks::kDeviceFile].TryGet<std::string>();
  device_properties->is_on_removable_device =
      properties[cros_disks::kDeviceIsOnRemovableDevice].TryGet<bool>();
  device_properties->is_auto_mountable =
      properties[cros_disks::kIsAutoMountable].TryGet<bool>();
  return true;
}

void CrosDisksClientImpl::AddMountCompletedHandler(
    base::RepeatingCallback<void(const MountEntry&)> callback) {
  proxy_->ConnectToSignal(cros_disks::kCrosDisksInterface,
                          cros_disks::kMountCompleted,
                          base::BindRepeating(&OnMountCompleted, callback),
                          base::BindOnce(&OnSignalConnected));
}

void CrosDisksClientImpl::Mount(const std::string& source,
                                const std::string& filesystem_type,
                                const std::vector<std::string>& options) {
  dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                               cros_disks::kMount);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(source);
  writer.AppendString(filesystem_type);
  writer.AppendArrayOfStrings(options);
  // No response from Mount method_call. cros-disks emits a |MountCompleted|
  // signal when the mount is done.
  proxy_->CallMethodAndBlock(&method_call,
                             dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
}

bool CrosDisksClientImpl::Unmount(const std::string& path,
                                  const std::vector<std::string>& options,
                                  uint32_t* result) {
  CHECK(result);
  dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                               cros_disks::kUnmount);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(path);
  writer.AppendArrayOfStrings(options);

  std::unique_ptr<dbus::Response> response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response.get()) {
    LOG(ERROR) << "Failed to call cros-disks D-Bus service";
    return false;
  }

  dbus::MessageReader reader(response.get());
  if (!reader.PopUint32(result)) {
    LOG(ERROR) << "Failed to decode cros-disks response";
    return false;
  }
  return true;
}

}  // namespace rmad
