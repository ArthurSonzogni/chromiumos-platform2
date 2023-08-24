// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fbpreprocessor_daemon.h"

#include <memory>

#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <dbus/bus.h>

#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

FbPreprocessorDaemon::FbPreprocessorDaemon() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system D-Bus";
    return;
  }
  manager_ = std::make_unique<Manager>(bus_.get());
}

}  // namespace fbpreprocessor
