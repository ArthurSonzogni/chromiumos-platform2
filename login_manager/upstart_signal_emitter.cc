// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/upstart_signal_emitter.h"

#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>
#include <dbus/error.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace login_manager {

namespace {

constexpr char kInterface[] = "com.ubuntu.Upstart0_6";
constexpr char kMethodName[] = "EmitEvent";
constexpr base::TimeDelta kDefaultTimeout = base::TimeDelta::Min();

}  // namespace

constexpr char UpstartSignalEmitter::kServiceName[] = "com.ubuntu.Upstart";
constexpr char UpstartSignalEmitter::kPath[] = "/com/ubuntu/Upstart";

UpstartSignalEmitter::UpstartSignalEmitter(dbus::ObjectProxy* proxy)
    : upstart_dbus_proxy_(proxy) {}

UpstartSignalEmitter::~UpstartSignalEmitter() = default;

std::unique_ptr<dbus::Response> UpstartSignalEmitter::TriggerImpulse(
    const std::string& name,
    const std::vector<std::string>& args_keyvals,
    TriggerMode mode) {
  dbus::Error error;
  return this->TriggerImpulseWithTimeoutAndError(name, args_keyvals, mode,
                                                 kDefaultTimeout, &error);
}

std::unique_ptr<dbus::Response>
UpstartSignalEmitter::TriggerImpulseWithTimeoutAndError(
    const std::string& name,
    const std::vector<std::string>& args_keyvals,
    TriggerMode mode,
    base::TimeDelta timeout,
    dbus::Error* error) {
  DLOG(INFO) << "Emitting " << name << " Upstart signal";

  dbus::MethodCall method_call(kInterface, kMethodName);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(name);
  writer.AppendArrayOfStrings(args_keyvals);
  // When this boolean is true, Upstart waits until all side-effects of the
  // event have completed instead of just returning after it's queued.
  writer.AppendBool(mode == TriggerMode::SYNC);
  int timeout_ms = timeout.is_min() ? dbus::ObjectProxy::TIMEOUT_USE_DEFAULT
                                    : timeout.InMilliseconds();
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response(
      upstart_dbus_proxy_->CallMethodAndBlock(&method_call, timeout_ms));
  if (!response.has_value()) {
    *error = std::move(response.error());
    return nullptr;
  }
  return std::move(response.value());
}

}  // namespace login_manager
