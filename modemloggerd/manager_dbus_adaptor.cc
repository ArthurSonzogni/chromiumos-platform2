// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/manager_dbus_adaptor.h"

namespace modemloggerd {

ManagerDBusAdaptor::ManagerDBusAdaptor(Manager* /*manager*/, dbus::Bus* bus)
    : ManagerAdaptorInterface(this),
      dbus_object_(
          nullptr,
          bus,
          org::chromium::Modemloggerd::ManagerAdaptor::GetObjectPath()) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

}  // namespace modemloggerd
