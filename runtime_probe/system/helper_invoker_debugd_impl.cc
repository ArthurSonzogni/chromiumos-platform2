// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/helper_invoker_debugd_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "runtime_probe/utils/pipe_utils.h"

namespace runtime_probe {

namespace {

constexpr auto kDebugdRunProbeHelperMethodName = "EvaluateProbeFunction";
constexpr auto kDebugdRunProbeHelperDefaultTimeoutMs = 10 * 1000;  // in ms

}  // namespace

bool RuntimeProbeHelperInvokerDebugdImpl::Invoke(
    const std::string& probe_statement, std::string* result) {
  dbus::Bus::Options ops;
  ops.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(ops)));
  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system D-Bus service.";
    return false;
  }

  dbus::ObjectProxy* object_proxy = bus->GetObjectProxy(
      debugd::kDebugdServiceName, dbus::ObjectPath(debugd::kDebugdServicePath));

  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               kDebugdRunProbeHelperMethodName);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(probe_statement);

  std::unique_ptr<dbus::Response> response = object_proxy->CallMethodAndBlock(
      &method_call, kDebugdRunProbeHelperDefaultTimeoutMs);
  if (!response) {
    LOG(ERROR) << "Failed to issue D-Bus call to method "
               << kDebugdRunProbeHelperMethodName
               << " of debugd D-Bus interface.";
    return false;
  }

  dbus::MessageReader reader(response.get());
  base::ScopedFD read_fd{};
  if (!reader.PopFileDescriptor(&read_fd)) {
    LOG(ERROR) << "Failed to read fd that represents the read end of the pipe"
                  " from debugd.";
    return false;
  }
  if (!ReadNonblockingPipeToString(read_fd.get(), result)) {
    LOG(ERROR) << "Cannot read result from helper";
    return false;
  }
  return true;
}

}  // namespace runtime_probe
