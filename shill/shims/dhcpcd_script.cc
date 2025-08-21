// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <base/at_exit.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <dbus/bus.h>

#include "shill/dbus-proxies.h"
#include "shill/shims/dhcpcd_script_utils.h"
#include "shill/shims/environment.h"

int main() {
  base::AtExitManager exit_manager;
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  CHECK(bus->Connect());
  auto proxy = org::chromium::flimflam::ManagerProxy(bus);

  shill::shims::Environment* environment =
      shill::shims::Environment::GetInstance();
  shill::shims::dhcpcd::ConfigMap config_map =
      shill::shims::dhcpcd::BuildConfigMap(environment);

  brillo::ErrorPtr error;
  bool ok = proxy.NotifyDHCPEvent(config_map, &error);
  if (!ok) {
    LOG(ERROR) << "DBus error: " << error->GetCode() << ": "
               << error->GetMessage();
  }

  bus->ShutdownAndBlock();

  if (!ok) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
