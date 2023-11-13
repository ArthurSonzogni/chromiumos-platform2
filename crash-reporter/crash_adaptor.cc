// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_adaptor.h"

#include <crash-reporter/dbus_adaptors/org.chromium.CrashReporterInterface.h>
#include <crash-reporter-client/crash-reporter/dbus-constants.h>

CrashAdaptor::CrashAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::CrashReporterInterfaceAdaptor(this),
      dbus_object_(
          nullptr,
          bus,
          dbus::ObjectPath(crash_reporter::kCrashReporterServicePath)) {
  if (bus.get()) {
    // Calling |RequestOwnershipAndBlock| should be alright because
    // crash-reporter is not long running daemon and on every udev
    // notification a new process is spawned to handle that udev
    // notification. Thus this is not blocking any other process.
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAndBlock();
    if (!bus->RequestOwnershipAndBlock(
            crash_reporter::kCrashReporterServiceName,
            dbus::Bus::ServiceOwnershipOptions::REQUIRE_PRIMARY)) {
      LOG(ERROR) << "Failed to take ownership of the crash reporter service";
    }
  }
}

CrashAdaptor::~CrashAdaptor() {
  dbus_object_.UnregisterAndBlock();
}
