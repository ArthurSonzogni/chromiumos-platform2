// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/dbus_adaptors/dbus_adaptor.h"

#include <utility>

namespace minios {

DBusService::DBusService(std::shared_ptr<MiniOsInterface> mini_os)
    : mini_os_(std::move(mini_os)) {}

bool DBusService::GetState(brillo::ErrorPtr* error, State* state_out) {
  return mini_os_->GetState(state_out, error);
}

DBusAdaptor::DBusAdaptor(std::unique_ptr<DBusService> dbus_service)
    : org::chromium::MiniOsInterfaceAdaptor(dbus_service.get()),
      dbus_service_(std::move(dbus_service)) {}

}  // namespace minios
