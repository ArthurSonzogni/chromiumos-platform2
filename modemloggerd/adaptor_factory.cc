// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/adaptor_factory.h"

#include <memory>

#include "modemloggerd/manager_dbus_adaptor.h"
#include "modemloggerd/modem_dbus_adaptor.h"

namespace modemloggerd {

std::unique_ptr<ModemAdaptorInterface> AdaptorFactory::CreateModemAdaptor(
    Modem* modem, dbus::Bus* bus) {
  return std::make_unique<ModemDBusAdaptor>(modem, bus);
}

std::unique_ptr<ManagerAdaptorInterface> AdaptorFactory::CreateManagerAdaptor(
    Manager* manager, dbus::Bus* bus) {
  return std::make_unique<ManagerDBusAdaptor>(manager, bus);
}

}  // namespace modemloggerd
