// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/daemon/regmon_daemon.h"

#include <memory>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "regmon/regmon/regmon_service.h"

namespace regmon {

RegmonDaemon::RegmonDaemon(std::unique_ptr<RegmonService> regmon)
    : brillo::DBusServiceDaemon(::regmon::kRegmonServiceName),
      regmon_(std::move(regmon)) {}

RegmonDaemon::~RegmonDaemon() = default;

void RegmonDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_ = std::make_unique<DBusAdaptor>(bus_, std::move(regmon_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler(/*descriptive_message=*/"RegisterAsync() failed",
                            /*failure_is_fatal=*/true));
}

}  // namespace regmon
