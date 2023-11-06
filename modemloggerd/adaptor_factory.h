// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_ADAPTOR_FACTORY_H_
#define MODEMLOGGERD_ADAPTOR_FACTORY_H_

#include <memory>

#include <dbus/bus.h>

#include "modemloggerd/adaptor_factory_interface.h"

namespace modemloggerd {

class AdaptorFactory : public AdaptorFactoryInterface {
 public:
  std::unique_ptr<ModemAdaptorInterface> CreateModemAdaptor(
      Modem* modem, dbus::Bus* bus) override;
  std::unique_ptr<ManagerAdaptorInterface> CreateManagerAdaptor(
      Manager* manager, dbus::Bus* bus) override;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_ADAPTOR_FACTORY_H_
