// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/obb-mounter/service.h"

#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <dbus/bus.h>
#include <dbus/message.h>

#include "arc/obb-mounter/mount.h"

namespace arc_obb_mounter {

namespace {

// TODO(hashimoto): Share these constants with chrome.
// D-Bus service constants.
const char kArcObbMounterInterface[] = "org.chromium.ArcObbMounterInterface";
const char kArcObbMounterServicePath[] = "/org/chromium/ArcObbMounter";
const char kArcObbMounterServiceName[] = "org.chromium.ArcObbMounter";

// Method names.
const char kMountObbMethod[] = "MountObb";
const char kUnmountObbMethod[] = "UnmountObb";

// Error names.
const char kErrorInvalidArgument[] =
    "org.chromium.ArcObbMounter.InvalidArgument";
const char kErrorFailed[] = "org.chromium.ArcObbMounter.Failed";

}  // namespace

Service::Service() : weak_ptr_factory_(this) {}

Service::~Service() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool Service::Initialize(scoped_refptr<dbus::Bus> bus) {
  bus_ = bus;
  // Export methods.
  exported_object_ =
      bus->GetExportedObject(dbus::ObjectPath(kArcObbMounterServicePath));
  if (!exported_object_->ExportMethodAndBlock(
          kArcObbMounterInterface, kMountObbMethod,
          base::Bind(&Service::MountObb, weak_ptr_factory_.GetWeakPtr()))) {
    LOG(ERROR) << "Failed to export MountObb method.";
    return false;
  }
  if (!exported_object_->ExportMethodAndBlock(
          kArcObbMounterInterface, kUnmountObbMethod,
          base::Bind(&Service::UnmountObb, weak_ptr_factory_.GetWeakPtr()))) {
    LOG(ERROR) << "Failed to export UnmountObb method.";
    return false;
  }
  // Request the ownership of the service name.
  if (!bus->RequestOwnershipAndBlock(kArcObbMounterServiceName,
                                     dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to own the service name";
    return false;
  }
  return true;
}

void Service::MountObb(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {
  dbus::MessageReader reader(method_call);
  std::string obb_file, mount_path;
  int32_t owner_gid = 0;
  if (!reader.PopString(&obb_file) || !reader.PopString(&mount_path) ||
      !reader.PopInt32(&owner_gid)) {
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, kErrorInvalidArgument, ""));
    return;
  }
  if (!arc_obb_mounter::MountObb(obb_file, mount_path, owner_gid)) {
    response_sender.Run(
        dbus::ErrorResponse::FromMethodCall(method_call, kErrorFailed, ""));
    return;
  }
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

void Service::UnmountObb(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender response_sender) {
  dbus::MessageReader reader(method_call);
  std::string mount_path;
  if (!reader.PopString(&mount_path)) {
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, kErrorInvalidArgument, ""));
    return;
  }
  if (!arc_obb_mounter::UnmountObb(mount_path)) {
    response_sender.Run(
        dbus::ErrorResponse::FromMethodCall(method_call, kErrorFailed, ""));
    return;
  }
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

}  // namespace arc_obb_mounter
