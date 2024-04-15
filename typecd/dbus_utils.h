// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DBUS_UTILS_H_
#define TYPECD_DBUS_UTILS_H_

#include <tuple>
#include <vector>

#include <dbus/bus.h>

namespace typecd {

// Struct to represent port data exposed by typecd D-Bus methods.
struct PortData {
  uint32_t port_num;
  std::tuple<uint8_t, uint8_t, uint8_t> pld;
  uint32_t current_mode;
  std::vector<uint32_t> partner_identity;
  uint16_t partner_revision;
  std::vector<std::tuple<uint16_t, uint32_t>> partner_alt_modes;
  std::vector<uint32_t> cable_identity;
  uint16_t cable_revision;
  std::vector<std::tuple<uint16_t, uint32_t>> cable_alt_modes;
};

// Helper to request the USB-C port count from typecd.
bool GetPortCount(dbus::ObjectProxy* typecd_proxy, uint32_t* port_count);

// Helper to request a USB-C port PLD from typecd.
bool GetPLD(dbus::ObjectProxy* typecd_proxy,
            std::tuple<uint8_t, uint8_t, uint8_t>* pld,
            uint32_t port);

// Helper to request the current mode of a USB-C port from typecd.
bool GetCurrentMode(dbus::ObjectProxy* typecd_proxy,
                    uint32_t* current_mode,
                    uint32_t port);

// Helper to request the BCD revision of a partner or cable on a given port.
bool GetRevision(dbus::ObjectProxy* typecd_proxy,
                 uint16_t* revision,
                 uint32_t port,
                 uint32_t recipient);

// Helper to request the identity response of a partner or cable on a given
// port.
bool GetIdentity(dbus::ObjectProxy* typecd_proxy,
                 std::vector<uint32_t>* identity,
                 uint32_t port,
                 uint32_t recipient);

// Helper to request to alternate modes of a partner or cable on a given port.
bool GetAltModes(dbus::ObjectProxy* typecd_proxy,
                 std::vector<std::tuple<uint16_t, uint32_t>>* alt_modes,
                 uint32_t port,
                 uint32_t recipient);

// Helper to request all available information typecd exposes about a given
// port, and what is connected to it.
bool GetPortData(dbus::ObjectProxy* typecd_proxy,
                 PortData* port,
                 uint32_t port_num);

void PrintRawPortData(PortData* port);

}  // namespace typecd

#endif  // TYPECD_DBUS_UTILS_H_
