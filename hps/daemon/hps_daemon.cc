// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <utility>

#include <chromeos/dbus/service_constants.h>

#include <hps/daemon/dbus_adaptor.h>

namespace hps {

HpsDaemon::HpsDaemon(std::unique_ptr<HPS> hps, uint32_t poll_time_ms)
    : brillo::DBusServiceDaemon(::hps::kHpsServiceName),
      hps_(std::move(hps)),
      poll_time_ms_(poll_time_ms) {}

HpsDaemon::~HpsDaemon() = default;

void HpsDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_, std::move(hps_), poll_time_ms_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace hps
