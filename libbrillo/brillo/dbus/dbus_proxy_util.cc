// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/dbus/dbus_proxy_util.h>

#include <utility>

#include <dbus/error.h>

namespace brillo {
namespace dbus_utils {

namespace {

base::expected<std::unique_ptr<dbus::Response>, dbus::Error>
CallDBusMethodInDbusThread(scoped_refptr<base::TaskRunner> task_runner,
                           dbus::ObjectProxy* proxy,
                           dbus::MethodCall* method_call,
                           int timeout_ms) {
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](dbus::ObjectProxy* proxy, dbus::MethodCall* method_call,
             int timeout_ms,
             base::expected<std::unique_ptr<dbus::Response>, dbus::Error>*
                 response,
             base::WaitableEvent* event) {
            *response = proxy->CallMethodAndBlock(method_call, timeout_ms);
            event->Signal();
          },
          base::Unretained(proxy), base::Unretained(method_call), timeout_ms,
          base::Unretained(&response), base::Unretained(&event)));
  event.Wait();
  return response;
}

std::unique_ptr<dbus::Response> ConvertResponse(
    base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response,
    dbus::Error* error) {
  if (!response.has_value() && error) {
    *error = std::move(response.error());
  }
  return std::move(response).value_or(nullptr);
}

}  // namespace

std::unique_ptr<dbus::Response> CallDBusMethod(scoped_refptr<dbus::Bus> bus,
                                               dbus::ObjectProxy* proxy,
                                               dbus::MethodCall* method_call,
                                               int timeout_ms) {
  return CallDBusMethodWithErrorResponse(bus, proxy, method_call, timeout_ms,
                                         /*error=*/nullptr);
}

std::unique_ptr<dbus::Response> CallDBusMethodWithErrorResponse(
    scoped_refptr<dbus::Bus> bus,
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* method_call,
    int timeout_ms,
    dbus::Error* error) {
  if (bus->HasDBusThread() &&
      !bus->GetDBusTaskRunner()->RunsTasksInCurrentSequence()) {
    return ConvertResponse(
        CallDBusMethodInDbusThread(bus->GetDBusTaskRunner(), proxy, method_call,
                                   timeout_ms),
        error);
  }
  return ConvertResponse(proxy->CallMethodAndBlock(method_call, timeout_ms),
                         error);
}

}  // namespace dbus_utils
}  // namespace brillo
