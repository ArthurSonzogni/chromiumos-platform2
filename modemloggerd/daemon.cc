//  Copyright 2023 The ChromiumOS Authors
//  Use of this source code is governed by a BSD-style license that can be
//  found in the LICENSE file.

#include "modemloggerd/daemon.h"

#include <memory>

#include "modemloggerd/dbus-constants.h"

namespace modemloggerd {

Daemon::Daemon() : DBusServiceDaemon(kModemloggerdServiceName) {}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_factory_ = std::make_unique<AdaptorFactory>();
  manager_ = std::make_unique<Manager>(bus_.get(), adaptor_factory_.get());
}

}  // namespace modemloggerd
