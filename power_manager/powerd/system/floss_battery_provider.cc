// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

#include "power_manager/powerd/system/floss_battery_provider.h"

namespace power_manager::system {

FlossBatteryProvider::FlossBatteryProvider() : weak_ptr_factory_(this) {}

void FlossBatteryProvider::Init(scoped_refptr<dbus::Bus> bus) {}

void FlossBatteryProvider::Reset() {}

void FlossBatteryProvider::UpdateDeviceBattery(const std::string& address,
                                               int level) {}

}  // namespace power_manager::system
