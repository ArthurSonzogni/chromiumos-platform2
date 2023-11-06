// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_MANAGER_DBUS_ADAPTOR_H_
#define MODEMLOGGERD_MANAGER_DBUS_ADAPTOR_H_

#include <dbus/bus.h>

#include "modemloggerd/adaptor_interfaces.h"

namespace modemloggerd {

class Manager;

class ManagerDBusAdaptor : public org::chromium::Modemloggerd::ManagerInterface,
                           public ManagerAdaptorInterface {
 public:
  ManagerDBusAdaptor(Manager* /*manager*/, dbus::Bus* bus);
  ManagerDBusAdaptor(const ManagerDBusAdaptor&) = delete;
  ManagerDBusAdaptor& operator=(const ManagerDBusAdaptor&) = delete;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_MANAGER_DBUS_ADAPTOR_H_
