// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_ADAPTOR_FACTORY_INTERFACE_H_
#define MODEMLOGGERD_ADAPTOR_FACTORY_INTERFACE_H_

#include <memory>

#include "modemloggerd/adaptor_interfaces.h"
#include "modemloggerd/dbus_bindings/org.chromium.Modemloggerd.Manager.h"

namespace modemloggerd {

class Modem;
class Manager;

// Interface for an object factory that creates an adaptor/proxy object.
class AdaptorFactoryInterface {
 public:
  virtual ~AdaptorFactoryInterface() = default;
  virtual std::unique_ptr<ModemAdaptorInterface> CreateModemAdaptor(
      Modem* modem, dbus::Bus* bus) = 0;
  virtual std::unique_ptr<ManagerAdaptorInterface> CreateManagerAdaptor(
      Manager* manager, dbus::Bus* bus) = 0;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_ADAPTOR_FACTORY_INTERFACE_H_
