// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_DBUS_PROXY_UTIL_H_
#define VM_TOOLS_CONCIERGE_DBUS_PROXY_UTIL_H_

// TODO(b/289932268): this file was removed from brillo, but due to urgent
// issue of the concierge, temporarily added back. Please do not use functions
// in this file in any of the new code. This should be removed sometime
// soon.

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace dbus {
class Error;
}  // namespace dbus

namespace vm_tools::concierge {

// This function calls a dbus method, and gets the response synchronously. It
// can be called from any thread, including the origin thread and dbus thread.
std::unique_ptr<dbus::Response> CallDBusMethod(scoped_refptr<dbus::Bus> bus,
                                               dbus::ObjectProxy* proxy,
                                               dbus::MethodCall* method_call,
                                               int timeout_ms);

std::unique_ptr<dbus::Response> CallDBusMethodWithErrorResponse(
    scoped_refptr<dbus::Bus> bus,
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* method_call,
    int timeout_ms,
    dbus::Error* error);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_DBUS_PROXY_UTIL_H_
