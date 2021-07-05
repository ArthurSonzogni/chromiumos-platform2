// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/dbus_utils_impl.h"

#include <memory>
#include <string>

#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <google/protobuf/message_lite.h>

namespace rmad {

bool DBusUtilsImpl::CallDBusMethod(const std::string& service_name,
                                   const std::string& service_path,
                                   const std::string& interface_name,
                                   const std::string& method_name,
                                   const google::protobuf::MessageLite& request,
                                   google::protobuf::MessageLite* reply,
                                   int timeout_ms) const {
  dbus::ObjectProxy* object_proxy = GetSystemBus()->GetObjectProxy(
      service_name, dbus::ObjectPath(service_path));

  dbus::MethodCall method_call(interface_name, method_name);
  dbus::MessageWriter writer(&method_call);
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    return false;
  }

  std::unique_ptr<dbus::Response> response =
      object_proxy->CallMethodAndBlock(&method_call, timeout_ms);
  if (!response) {
    return false;
  }
  dbus::MessageReader reader(response.get());
  return reader.PopArrayOfBytesAsProto(reply);
}

}  // namespace rmad
