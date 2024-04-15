// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/dbus_utils.h"

#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/bus.h>
#include <dbus/typecd/dbus-constants.h>

#include <iostream>
#include <memory>
#include <tuple>
#include <vector>

#include "typecd/port.h"
#include "typecd/utils.h"

namespace typecd {

bool GetPortCount(dbus::ObjectProxy* typecd_proxy, uint32_t* port_count) {
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName,
          typecd::kTypecdGetPortCountMethod, &error);

  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopUint32(port_count)) {
    return false;
  }

  return true;
}

bool GetPLD(dbus::ObjectProxy* typecd_proxy,
            std::tuple<uint8_t, uint8_t, uint8_t>* pld,
            uint32_t port) {
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName, typecd::kTypecdGetPLDMethod,
          &error, port);

  dbus::MessageReader struct_reader(nullptr);
  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopStruct(&struct_reader)) {
    return false;
  }

  uint8_t panel, h_position, v_position;
  if (!struct_reader.PopByte(&panel) || !struct_reader.PopByte(&h_position) ||
      !struct_reader.PopByte(&v_position)) {
    return false;
  }

  *pld = std::make_tuple(panel, h_position, v_position);
  return true;
}

bool GetCurrentMode(dbus::ObjectProxy* typecd_proxy,
                    uint32_t* current_mode,
                    uint32_t port) {
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName,
          typecd::kTypecdGetCurrentModeMethod, &error, port);

  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopUint32(current_mode)) {
    return false;
  }

  return true;
}

bool GetRevision(dbus::ObjectProxy* typecd_proxy,
                 uint16_t* revision,
                 uint32_t port,
                 uint32_t recipient) {
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName,
          typecd::kTypecdGetRevisionMethod, &error, port, recipient);

  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopUint16(revision)) {
    return false;
  }

  return true;
}

bool GetIdentity(dbus::ObjectProxy* typecd_proxy,
                 std::vector<uint32_t>* identity,
                 uint32_t port,
                 uint32_t recipient) {
  identity->clear();
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName,
          typecd::kTypecdGetIdentityMethod, &error, port, recipient);

  dbus::MessageReader array_reader(nullptr);
  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopArray(&array_reader)) {
    return false;
  }

  while (array_reader.HasMoreData()) {
    uint32_t dbus_response;
    if (!array_reader.PopUint32(&dbus_response)) {
      identity->clear();
      return false;
    }

    identity->push_back(dbus_response);
  }

  return true;
}

bool GetAltModes(dbus::ObjectProxy* typecd_proxy,
                 std::vector<std::tuple<uint16_t, uint32_t>>* alt_modes,
                 uint32_t port,
                 uint32_t recipient) {
  alt_modes->clear();
  brillo::ErrorPtr error = nullptr;
  std::unique_ptr<dbus::Response> response =
      brillo::dbus_utils::CallMethodAndBlock(
          typecd_proxy, typecd::kTypecdServiceName,
          typecd::kTypecdGetAltModesMethod, &error, port, recipient);

  dbus::MessageReader array_reader(nullptr);
  if (error != nullptr ||
      !dbus::MessageReader(response.get()).PopArray(&array_reader)) {
    return false;
  }

  while (array_reader.HasMoreData()) {
    uint16_t svid;
    uint32_t vdo;
    dbus::MessageReader struct_reader(nullptr);
    if (!array_reader.PopStruct(&struct_reader) ||
        !struct_reader.PopUint16(&svid) || !struct_reader.PopUint32(&vdo)) {
      alt_modes->clear();
      return false;
    }

    alt_modes->push_back(std::make_tuple(svid, vdo));
  }

  return true;
}

bool GetPortData(dbus::ObjectProxy* typecd_proxy,
                 PortData* port,
                 uint32_t port_num) {
  port->port_num = port_num;
  if (!GetPLD(typecd_proxy, &port->pld, port_num)) {
    return false;
  }

  if (!GetCurrentMode(typecd_proxy, &port->current_mode, port_num)) {
    return false;
  }

  if (!GetIdentity(typecd_proxy, &port->partner_identity, port_num,
                   (uint32_t)typecd::Recipient::kPartner)) {
    return false;
  }

  if (!GetRevision(typecd_proxy, &port->partner_revision, port_num,
                   (uint32_t)typecd::Recipient::kPartner)) {
    return false;
  }

  if (!GetAltModes(typecd_proxy, &port->partner_alt_modes, port_num,
                   (uint32_t)typecd::Recipient::kPartner)) {
    return false;
  }

  if (!GetIdentity(typecd_proxy, &port->cable_identity, port_num,
                   (uint32_t)typecd::Recipient::kCable)) {
    return false;
  }

  if (!GetRevision(typecd_proxy, &port->cable_revision, port_num,
                   (uint32_t)typecd::Recipient::kCable)) {
    return false;
  }

  if (!GetAltModes(typecd_proxy, &port->cable_alt_modes, port_num,
                   (uint32_t)typecd::Recipient::kCable)) {
    return false;
  }

  return true;
}

void PrintRawPortData(PortData* port) {
  // Print port data.
  std::cout << "Port: " << port->port_num << std::endl;
  std::cout << "PLD: " << unsigned(get<0>(port->pld)) << ", "
            << unsigned(get<1>(port->pld)) << ", "
            << unsigned(get<2>(port->pld)) << std::endl;
  std::cout << "Active Mode: " << port->port_num << std::endl;

  // Print partner data.
  std::cout << "SOP Revision: 0x" << FormatHexString(port->partner_revision, 4)
            << std::endl;
  std::cout << "SOP Identity: " << std::endl;
  for (uint32_t vdo : port->partner_identity) {
    std::cout << "  0x" << FormatHexString(vdo, 8) << std::endl;
  }
  std::cout << "SOP Modes (SVID/VDO): " << std::endl;
  for (std::tuple<uint16_t, uint32_t> mode : port->partner_alt_modes) {
    std::cout << "  0x" << FormatHexString(get<0>(mode), 4) << "/0x"
              << FormatHexString(get<1>(mode), 8) << std::endl;
  }

  // Print cable data.
  std::cout << "SOP' Revision: 0x" << FormatHexString(port->cable_revision, 4)
            << std::endl;
  std::cout << "SOP' Identity: " << std::endl;
  for (uint32_t vdo : port->cable_identity) {
    std::cout << "  0x" << FormatHexString(vdo, 8) << std::endl;
  }
  std::cout << "SOP' Modes (SVID/VDO): " << std::endl;
  for (std::tuple<uint16_t, uint32_t> mode : port->cable_alt_modes) {
    std::cout << "  0x" << FormatHexString(get<0>(mode), 4) << "/0x"
              << FormatHexString(get<1>(mode), 8) << std::endl;
  }

  std::cout << std::endl;
}

}  // namespace typecd
