// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_DBUS_UTILS_H_
#define RMAD_UTILS_DBUS_UTILS_H_

#include <memory>
#include <string>

#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace rmad {

constexpr int DBUS_DEFAULT_TIMEOUT_MS = 10000;  // 10 seconds

bool CallDBusMethod(const std::string& service_name,
                    const std::string& service_path,
                    const std::string& interface_name,
                    const std::string& method_name,
                    const google::protobuf::MessageLite& request,
                    google::protobuf::MessageLite* reply,
                    int timeout_ms = DBUS_DEFAULT_TIMEOUT_MS) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  auto bus = base::MakeRefCounted<dbus::Bus>(options);
  dbus::ObjectProxy* object_proxy =
      bus->GetObjectProxy(service_name, dbus::ObjectPath(service_path));

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

#endif  // RMAD_UTILS_DBUS_UTILS_H_
