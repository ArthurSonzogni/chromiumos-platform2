// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/dbus_adaptor.h"

#include <utility>

#include <base/check.h>
#include <base/notreached.h>
#include <dbus/object_path.h>
#include <dbus/printscanmgr/dbus-constants.h>

namespace printscanmgr {

DbusAdaptor::DbusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::printscanmgrAdaptor(this),
      dbus_object_(/*object_manager=*/nullptr,
                   bus,
                   dbus::ObjectPath(kPrintscanmgrServicePath)) {}
DbusAdaptor::~DbusAdaptor() = default;

void DbusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction
        completion_action) {
  brillo::dbus_utils::DBusInterface* interface =
      dbus_object_.AddOrGetInterface(kPrintscanmgrInterface);
  DCHECK(interface);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(completion_action));
}

int32_t DbusAdaptor::CupsAddAutoConfiguredPrinter(const std::string& name,
                                                  const std::string& uri) {
  NOTIMPLEMENTED() << " CupsAddAutoConfiguredPrinter not implemented.";

  return -1;
}

int32_t DbusAdaptor::CupsAddManuallyConfiguredPrinter(
    const std::string& name,
    const std::string& uri,
    const std::vector<uint8_t>& ppd_contents) {
  NOTIMPLEMENTED() << " CupsAddManuallyConfiguredPrinter not implemented.";

  return -1;
}

bool DbusAdaptor::CupsRemovePrinter(const std::string& name) {
  NOTIMPLEMENTED() << " CupsRemovePrinter not implemented.";

  return false;
}

std::vector<uint8_t> DbusAdaptor::CupsRetrievePpd(const std::string& name) {
  NOTIMPLEMENTED() << " CupsRetrievePpd not implemented.";

  return {};
}

bool DbusAdaptor::PrintscanDebugSetCategories(brillo::ErrorPtr* error,
                                              uint32_t categories) {
  NOTIMPLEMENTED() << " PrintscanDebugSetCategories not implemented.";

  return false;
}

}  // namespace printscanmgr
