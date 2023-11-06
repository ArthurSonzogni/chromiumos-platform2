// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_MANAGER_H_
#define MODEMLOGGERD_MANAGER_H_

#include <memory>
#include <vector>

#include <dbus/bus.h>

#include "modemloggerd/adaptor_factory.h"
#include "modemloggerd/dbus_bindings/org.chromium.Modemloggerd.Manager.h"
#include "modemloggerd/logger_interface.h"

namespace modemloggerd {

class Manager {
 public:
  Manager(dbus::Bus* bus, AdaptorFactoryInterface* adaptor_factory);
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

 private:
  void UpdateAvailableModemsProperty();

  dbus::Bus* bus_;
  std::unique_ptr<ManagerAdaptorInterface> dbus_adaptor_;
  std::vector<std::unique_ptr<LoggerInterface>> available_modems_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_MANAGER_H_
