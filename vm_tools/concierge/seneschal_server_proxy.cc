// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/seneschal_server_proxy.h"

#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <seneschal/proto_bindings/seneschal_service.pb.h>

namespace vm_tools {
namespace concierge {

// static
std::unique_ptr<SeneschalServerProxy> SeneschalServerProxy::Create(
    dbus::ObjectProxy* seneschal_proxy, uint32_t port, uint32_t accept_cid) {
  dbus::MethodCall method_call(vm_tools::seneschal::kSeneschalInterface,
                               vm_tools::seneschal::kStartServerMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::seneschal::StartServerRequest request;
  request.mutable_vsock()->set_port(port);
  request.mutable_vsock()->set_accept_cid(accept_cid);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode StartServerRequest protobuf";
    return nullptr;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      seneschal_proxy->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send StartServer message to seneschal service";
    return nullptr;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::seneschal::StartServerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse StartServerResponse protobuf";
    return nullptr;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to start server: " << response.failure_reason();
    return nullptr;
  }

  return std::unique_ptr<SeneschalServerProxy>(
      new SeneschalServerProxy(seneschal_proxy, response.handle()));
}

SeneschalServerProxy::SeneschalServerProxy(dbus::ObjectProxy* seneschal_proxy,
                                           uint32_t handle)
    : seneschal_proxy_(seneschal_proxy), handle_(handle) {}

SeneschalServerProxy::~SeneschalServerProxy() {
  dbus::MethodCall method_call(vm_tools::seneschal::kSeneschalInterface,
                               vm_tools::seneschal::kStopServerMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::seneschal::StopServerRequest request;
  request.set_handle(handle_);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode StopServerRequest protobuf";
    return;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      seneschal_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send StopServer message to seneschal service";
    return;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::seneschal::StopServerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse StopServerResponse protobuf";
    return;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to stop server " << handle_ << ": "
               << response.failure_reason();
    return;
  }
}

}  // namespace concierge
}  // namespace vm_tools
