// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/shadercached_helper.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/waitable_event.h>
#include <dbus/message.h>
#include <dbus/scoped_dbus_error.h>
#include <dbus/shadercached/dbus-constants.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <vm_protos/proto_bindings/vm_host.pb.h>

namespace vm_tools::cicerone {

namespace {
void ShaderCacheMountStatusChanged(
    std::string* error_out,
    base::WaitableEvent* event,
    bool expected_mount,
    const shadercached::ShaderCacheMountStatus& mount_status) {
  if (!mount_status.error().empty()) {
    *error_out = mount_status.error();
  } else if (mount_status.mounted() == expected_mount) {
    *error_out = "";
  } else {
    // |mounted| does not equate to |expected_mount| despite having no error
    LOG(WARNING) << "Unexpected mount status mismatch for "
                 << mount_status.vm_name();
    *error_out =
        base::StringPrintf("Unexpected mount status, expected: %d, got %d",
                           expected_mount, mount_status.mounted());
  }

  event->Signal();
}
}  // namespace

ShadercachedHelper::ShadercachedHelper(dbus::ObjectProxy* shadercached_proxy) {
  connected_ = false;
  shadercached_proxy->ConnectToSignal(
      shadercached::kShaderCacheInterface,
      shadercached::kShaderCacheMountStatusChanged,
      base::BindRepeating(&ShadercachedHelper::MountStatusChanged,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&ShadercachedHelper::ConnectedToShadercached,
                     weak_ptr_factory_.GetWeakPtr()));
}

void ShadercachedHelper::ConnectedToShadercached(const std::string& interface,
                                                 const std::string& signal,
                                                 bool success) {
  connected_ = success;
  LOG_IF(ERROR, !success)
      << "Failed to create ShadercachedHelper, connection to signal failed";
}

void ShadercachedHelper::InstallShaderCache(
    const std::string& owner_id,
    const std::string& vm_name,
    const vm_tools::container::InstallShaderCacheRequest* request,
    std::string* error_out,
    base::WaitableEvent* event,
    dbus::ObjectProxy* shadercached_proxy) {
  LOG(INFO) << "InstallShaderCache called";

  if (!connected_) {
    *error_out = "Not connected to shadercached signals";
    event->Signal();
    return;
  }

  ShadercachedHelper::CallbackCondition condition{
      .vm_name = vm_name,
      .owner_id = owner_id,
      .steam_app_id = request->steam_app_id()};
  if (request->wait() && !AddCallback(condition, error_out, event)) {
    event->Signal();
    return;
  }

  dbus::MethodCall method_call(shadercached::kShaderCacheInterface,
                               shadercached::kInstallMethod);
  dbus::MessageWriter shadercached_writer(&method_call);
  shadercached::InstallRequest shader_request;
  shader_request.set_mount(request->mount());
  shader_request.set_steam_app_id(request->steam_app_id());
  shader_request.set_vm_name(vm_name);
  shader_request.set_vm_owner_id(owner_id);
  shadercached_writer.AppendProtoAsArrayOfBytes(shader_request);

  dbus::ScopedDBusError error;
  std::unique_ptr<dbus::Response> dbus_response =
      shadercached_proxy->CallMethodAndBlockWithErrorDetails(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, &error);
  if (!dbus_response) {
    *error_out =
        base::StringPrintf("%s %s: %s", shadercached::kShaderCacheInterface,
                           error.name(), error.message());
    if (request->wait()) {
      mount_callbacks_.erase(condition);
    }
    event->Signal();
    return;
  }

  if (!request->wait()) {
    // Only signal if we don't have to wait. If wait is set, signal will happen
    // at ShaderCacheMountStatusChanged.
    *error_out = "";
    event->Signal();
  }
}

void ShadercachedHelper::UninstallShaderCache(
    const std::string& owner_id,
    const std::string& vm_name,
    const vm_tools::container::UninstallShaderCacheRequest* request,
    std::string* error_out,
    base::WaitableEvent* event,
    dbus::ObjectProxy* shadercached_proxy_) {
  LOG(INFO) << "UninstallShaderCache called";

  if (!connected_) {
    *error_out = "Not connected to shadercached signals";
    event->Signal();
    return;
  }

  dbus::MethodCall method_call(shadercached::kShaderCacheInterface,
                               shadercached::kUninstallMethod);
  dbus::MessageWriter shadercached_writer(&method_call);
  shadercached::UninstallRequest shader_request;
  shader_request.set_steam_app_id(request->steam_app_id());
  shadercached_writer.AppendProtoAsArrayOfBytes(shader_request);

  dbus::ScopedDBusError error;
  std::unique_ptr<dbus::Response> dbus_response =
      shadercached_proxy_->CallMethodAndBlockWithErrorDetails(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, &error);
  if (!dbus_response) {
    *error_out =
        base::StringPrintf("%s %s: %s", shadercached::kShaderCacheInterface,
                           error.name(), error.message());
    event->Signal();
    return;
  }

  // We do not need to wait for unmount signal here. Unmount was completed in
  // the D-Bus call above and unmount errors would have appeared in D-Bus
  // errors.
  // This does not wait for DLC uninstallation to complete because game
  // uninstalls are independent and orthogonal to DLC uninstalls.
  *error_out = "";
  event->Signal();
}

bool ShadercachedHelper::AddCallback(const CallbackCondition& condition,
                                     std::string* error_out,
                                     base::WaitableEvent* event_to_notify) {
  // Do not queue callback if there is already one. This is to prevent memory
  // increase from misbehaving user (ex. spamming game launches) when DLC
  // download is taking time.
  if (mount_callbacks_.find(condition) != mount_callbacks_.end()) {
    *error_out = "Already installing shader cache for the Steam app";
    return false;
  }

  mount_callbacks_[condition] = base::BindOnce(
      &ShaderCacheMountStatusChanged, error_out, event_to_notify, true);
  return true;
}

void ShadercachedHelper::MountStatusChanged(dbus::Signal* signal) {
  shadercached::ShaderCacheMountStatus mount_status;
  auto reader = dbus::MessageReader(signal);
  if (!reader.PopArrayOfBytesAsProto(&mount_status)) {
    LOG(WARNING) << "Failed to parse ShaderCacheMountStatus";
    return;
  }

  // Generate the key for this signal and find it
  CallbackCondition condition{
      .vm_name = mount_status.vm_name(),
      .owner_id = mount_status.vm_owner_id(),
      .steam_app_id = mount_status.steam_app_id(),
  };
  if (mount_callbacks_.find(condition) != mount_callbacks_.end()) {
    LOG(INFO) << "Notifying shader cache mount callback for VM "
              << mount_status.vm_name();
    std::move(mount_callbacks_[condition]).Run(mount_status);
    mount_callbacks_.erase(condition);
  } else {
    LOG(WARNING) << "No callback found for " << mount_status.steam_app_id();
  }
}

}  // namespace vm_tools::cicerone
