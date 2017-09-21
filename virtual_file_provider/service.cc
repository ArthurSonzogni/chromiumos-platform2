// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_file_provider/service.h"

#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace virtual_file_provider {

Service::Service(const base::FilePath& fuse_mount_path)
    : fuse_mount_path_(fuse_mount_path), weak_ptr_factory_(this) {
  thread_checker_.DetachFromThread();
}

Service::~Service() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (bus_)
    bus_->ShutdownAndBlock();
}

bool Service::Initialize() {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Connect the bus.
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to initialize D-Bus connection.";
    return false;
  }
  request_handler_proxy_ = bus_->GetObjectProxy(
      chromeos::kVirtualFileRequestServiceName,
      dbus::ObjectPath(chromeos::kVirtualFileRequestServicePath));
  // Export methods.
  exported_object_ = bus_->GetExportedObject(
      dbus::ObjectPath(kVirtualFileProviderServicePath));
  if (!exported_object_->ExportMethodAndBlock(
          kVirtualFileProviderInterface,
          kOpenFileMethod,
          base::Bind(&Service::OpenFile, weak_ptr_factory_.GetWeakPtr()))) {
    LOG(ERROR) << "Failed to export OpenFile method.";
    return false;
  }
  // Request the ownership of the service name.
  if (!bus_->RequestOwnershipAndBlock(kVirtualFileProviderServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to own the service name";
    return false;
  }
  return true;
}

void Service::SendReadRequest(const std::string& id,
                              int64_t offset,
                              int64_t size,
                              base::ScopedFD fd) {
  DCHECK(thread_checker_.CalledOnValidThread());
  dbus::MethodCall method_call(
      chromeos::kVirtualFileRequestServiceInterface,
      chromeos::kVirtualFileRequestServiceHandleReadRequestMethod);

  dbus::MessageWriter writer(&method_call);
  writer.AppendString(id);
  writer.AppendInt64(offset);
  writer.AppendInt64(size);
  dbus::FileDescriptor dbus_fd(fd.get());
  dbus_fd.CheckValidity();
  writer.AppendFileDescriptor(dbus_fd);
  request_handler_proxy_->CallMethod(
      &method_call,
      dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      dbus::ObjectProxy::EmptyResponseCallback());
}

void Service::SendIdReleased(const std::string& id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  dbus::MethodCall method_call(
      chromeos::kVirtualFileRequestServiceInterface,
      chromeos::kVirtualFileRequestServiceHandleIdReleasedMethod);

  dbus::MessageWriter writer(&method_call);
  writer.AppendString(id);
  request_handler_proxy_->CallMethod(
      &method_call,
      dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      dbus::ObjectProxy::EmptyResponseCallback());
}

void Service::OpenFile(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Generate a new ID.
  std::string id = base::GenerateGUID();
  // An ID corresponds to a file name in the FUSE file system.
  base::FilePath path = fuse_mount_path_.AppendASCII(id);
  // Create a new FD associated with the ID.
  base::ScopedFD fd(HANDLE_EINTR(open(path.value().c_str(),
                                      O_RDONLY | O_CLOEXEC)));

  // Send response.
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendString(id);
  dbus::FileDescriptor dbus_fd(fd.get());
  dbus_fd.CheckValidity();
  writer.AppendFileDescriptor(dbus_fd);
  response_sender.Run(std::move(response));
}

}  // namespace virtual_file_provider
