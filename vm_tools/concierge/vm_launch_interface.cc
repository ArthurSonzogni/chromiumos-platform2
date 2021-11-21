// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_launch_interface.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <brillo/dbus/dbus_proxy_util.h>
#include <chromeos/dbus/vm_launch/dbus-constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/scoped_dbus_error.h>
#include <vm_launch/proto_bindings/launch.pb.h>

#include "vm_tools/common/vm_id.h"

namespace vm_tools {
namespace concierge {

VmLaunchInterface::VmLaunchInterface(scoped_refptr<dbus::Bus> bus)
    : bus_(bus),
      proxy_(bus_->GetObjectProxy(
          launch::kVmLaunchServiceName,
          dbus::ObjectPath(launch::kVmLaunchServicePath))) {}

VmLaunchInterface::~VmLaunchInterface() = default;

std::string VmLaunchInterface::GetWaylandSocketForVm(const VmId& vm_id,
                                                     bool is_termina) {
  dbus::MethodCall method_call(
      launch::kVmLaunchServiceInterface,
      launch::kVmLaunchServiceStartWaylandServerMethod);
  dbus::MessageWriter writer(&method_call);

  launch::StartWaylandServerRequest request;
  if (!is_termina && vm_id.name() == "borealis") {
    request.set_vm_type(launch::BOREALIS);
  } else {
    request.set_vm_type(launch::UNKNOWN);
  }
  request.set_owner_id(vm_id.owner_id());

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode StartWaylandServerRequest protobuf";
    return "";
  }

  dbus::ScopedDBusError dbus_error;
  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallDBusMethodWithErrorResponse(
          bus_, proxy_, &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
          &dbus_error);
  if (!dbus_response) {
    if (dbus_error.is_set()) {
      LOG(ERROR) << "StartWaylandServerRequest call failed: "
                 << dbus_error.name() << " (" << dbus_error.message() << ")";
    } else {
      LOG(ERROR) << "Failed to send StartWaylandServerRequest message to "
                    "vm_launch service";
    }
    return "";
  }
  dbus::MessageReader reader(dbus_response.get());
  launch::StartWaylandServerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse StartWaylandServerResponse protobuf";
    return "";
  }

  return response.server().path();
}

}  // namespace concierge
}  // namespace vm_tools
