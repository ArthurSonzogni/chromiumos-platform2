// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_ADAPTOR_INTERFACES_H_
#define MODEMLOGGERD_ADAPTOR_INTERFACES_H_

#include "modemloggerd/dbus_bindings/org.chromium.Modemloggerd.Manager.h"
#include "modemloggerd/dbus_bindings/org.chromium.Modemloggerd.Modem.h"

namespace modemloggerd {

class ModemAdaptorInterface : public org::chromium::Modemloggerd::ModemAdaptor {
 public:
  explicit ModemAdaptorInterface(
      org::chromium::Modemloggerd::ModemInterface* interface)
      : org::chromium::Modemloggerd::ModemAdaptor(interface) {}
  virtual ~ModemAdaptorInterface() = default;

  virtual dbus::ObjectPath object_path() const = 0;
};

class ManagerAdaptorInterface
    : public org::chromium::Modemloggerd::ManagerAdaptor {
 public:
  explicit ManagerAdaptorInterface(
      org::chromium::Modemloggerd::ManagerInterface* interface)
      : org::chromium::Modemloggerd::ManagerAdaptor(interface) {}
  virtual ~ManagerAdaptorInterface() = default;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_ADAPTOR_INTERFACES_H_
