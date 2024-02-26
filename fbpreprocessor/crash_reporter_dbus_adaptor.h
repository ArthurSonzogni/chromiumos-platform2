// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_CRASH_REPORTER_DBUS_ADAPTOR_H_
#define FBPREPROCESSOR_CRASH_REPORTER_DBUS_ADAPTOR_H_

#include <string>

#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

class CrashReporterDBusAdaptor {
 public:
  CrashReporterDBusAdaptor(Manager* manager, dbus::Bus* bus);

 private:
  void OnFirmwareDumpCreated(dbus::Signal* signal) const;

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success) const;

  Manager* manager_;

  // Proxy to receive D-Bus signals from crash-reporter.
  scoped_refptr<dbus::ObjectProxy> crash_reporter_proxy_;

  base::WeakPtrFactory<CrashReporterDBusAdaptor> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_CRASH_REPORTER_DBUS_ADAPTOR_H_
