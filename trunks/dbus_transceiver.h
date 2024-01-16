// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_DBUS_TRANSCEIVER_H_
#define TRUNKS_DBUS_TRANSCEIVER_H_

#include <string>

#include "trunks/command_transceiver.h"

namespace trunks {

// DbusTransceiver is an interface that sends commands to a trunks D-Bus and
// receives responses. It can operate synchronously or asynchronously.
class DbusTransceiver : public CommandTransceiver {
 public:
  ~DbusTransceiver() override {}

  // Start a trunks event.
  virtual void StartEvent(const std::string& event) = 0;

  // Stop a trunks event.
  virtual void StopEvent(const std::string& event) = 0;
};

}  // namespace trunks

#endif  // TRUNKS_DBUS_TRANSCEIVER_H_
