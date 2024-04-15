// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/flag_helper.h>
#include <dbus/typecd/dbus-constants.h>

#include <iostream>

#include "typecd/dbus_utils.h"
#include "typecd/port.h"

namespace {}  // namespace

int main(int argc, char* argv[]) {
  // Flags
  DEFINE_bool(status, false,
              "Display information about the system's USB-C ports.");
  brillo::FlagHelper::Init(
      argc, argv,
      "typecd_tool is an executable for interfacing with the Type-C Daemon.");

  // Setup D-Bus Proxy.
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());
  dbus::ObjectProxy* typecd_proxy = bus->GetObjectProxy(
      typecd::kTypecdServiceName, dbus::ObjectPath(typecd::kTypecdServicePath));

  if (FLAGS_status) {
    std::vector<typecd::PortData> port_data = {};

    // Request port data from typecd.
    uint32_t port_count;
    if (!typecd::GetPortCount(typecd_proxy, &port_count)) {
      std::cerr << "Failed to get port count" << std::endl;
      exit(1);
    }

    for (int i = 0; i < port_count; i++) {
      typecd::PortData port;
      if (!typecd::GetPortData(typecd_proxy, &port, i)) {
        std::cerr << "Failed to get data for port" << i << std::endl;
        port_data.clear();
        exit(1);
      }
      port_data.push_back(port);
    }

    // Print unformatted port data.
    for (int i = 0; i < port_data.size(); i++)
      typecd::PrintRawPortData(&port_data[i]);
  }
}
