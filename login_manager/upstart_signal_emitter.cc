// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/upstart_signal_emitter.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace login_manager {

const char UpstartSignalEmitter::kServiceName[] = "com.ubuntu.Upstart";
const char UpstartSignalEmitter::kPath[] = "/com/ubuntu/Upstart";
const char UpstartSignalEmitter::kInterface[] = "com.ubuntu.Upstart0_6";
const char UpstartSignalEmitter::kMethodName[] = "EmitEvent";

UpstartSignalEmitter::UpstartSignalEmitter(dbus::ObjectProxy* proxy)
    : upstart_dbus_proxy_(proxy) {
}

UpstartSignalEmitter::~UpstartSignalEmitter() {}

std::unique_ptr<dbus::Response> UpstartSignalEmitter::TriggerImpulse(
    const std::string& name,
    const std::vector<std::string>& args_keyvals,
    TriggerMode mode) {
  return EmitSignal(name, args_keyvals, mode);
}

std::unique_ptr<dbus::Response> UpstartSignalEmitter::EmitSignal(
    const std::string& signal_name,
    const std::vector<std::string>& args_keyvals,
    TriggerMode mode) {
  DLOG(INFO) << "Emitting " << signal_name << " Upstart signal";

  dbus::MethodCall method_call(UpstartSignalEmitter::kInterface,
                               UpstartSignalEmitter::kMethodName);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(signal_name);
  writer.AppendArrayOfStrings(args_keyvals);
  writer.AppendBool(true);

  switch (mode) {
    case TriggerMode::SYNC:
      return upstart_dbus_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    case TriggerMode::ASYNC:
      upstart_dbus_proxy_->CallMethod(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
          dbus::ObjectProxy::EmptyResponseCallback());
      return nullptr;
  }
  NOTREACHED() << "Invalid trigger mode " << mode;
  return nullptr;
}

}  // namespace login_manager
