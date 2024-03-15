// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/dbus_manager.h"

#include <string>
#include <tuple>

#include <base/logging.h>

#include "typecd/peripheral.h"

namespace typecd {

DBusManager::DBusManager(brillo::dbus_utils::DBusObject* dbus_object)
    : org::chromium::typecdAdaptor(this) {
  RegisterWithDBusObject(dbus_object);
}

void DBusManager::NotifyConnected(DeviceConnectedType type) {
  SendDeviceConnectedSignal(static_cast<uint32_t>(type));
}

void DBusManager::NotifyCableWarning(CableWarningType type) {
  SendCableWarningSignal(static_cast<uint32_t>(type));
}

bool DBusManager::GetAltModes(
    brillo::ErrorPtr* err,
    uint32_t port,
    uint32_t recipient,
    std::vector<std::tuple<uint16_t, uint32_t>>* alt_modes) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  alt_modes->clear();
  for (AltMode* mode : port_mgr_->GetAltModes(port, recipient))
    alt_modes->push_back(std::make_tuple(mode->GetSVID(), mode->GetVDO()));

  return true;
}

bool DBusManager::GetCurrentMode(brillo::ErrorPtr* err,
                                 uint32_t port,
                                 uint32_t* mode) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  if (!port_mgr_->HasPartner(port)) {
    *mode = ((uint32_t)USBCMode::kDisconnected);
  } else {
    switch (port_mgr_->GetCurrentMode(port)) {
      case TypeCMode::kDP:
        *mode = ((uint32_t)USBCMode::kDP);
        break;
      case TypeCMode::kTBT:
        *mode = ((uint32_t)USBCMode::kTBT);
        break;
      case TypeCMode::kUSB4:
        *mode = ((uint32_t)USBCMode::kUSB4);
        break;
      default:
        *mode = ((uint32_t)USBCMode::kNone);
        break;
    }
  }

  return true;
}

bool DBusManager::GetIdentity(brillo::ErrorPtr* err,
                              uint32_t port,
                              uint32_t recipient,
                              std::vector<uint32_t>* identity) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  identity->clear();
  for (uint32_t vdo : port_mgr_->GetIdentity(port, recipient))
    identity->push_back(vdo);

  return true;
}

bool DBusManager::GetPLD(brillo::ErrorPtr* err,
                         uint32_t port,
                         std::tuple<uint8_t, uint8_t, uint8_t>* pld) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  *pld = std::make_tuple(((uint8_t)port_mgr_->GetPanel(port)),
                         ((uint8_t)port_mgr_->GetHorizontalPosition(port)),
                         ((uint8_t)port_mgr_->GetVerticalPosition(port)));
  return true;
}

bool DBusManager::GetPortCount(brillo::ErrorPtr* err, uint32_t* port_count) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  *port_count = port_mgr_->GetPortCount();
  return true;
}

bool DBusManager::GetRevision(brillo::ErrorPtr* err,
                              uint32_t port,
                              uint32_t recipient,
                              uint16_t* revision) {
  if (!port_mgr_) {
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_mgr",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  switch (port_mgr_->GetPDRevision(port, recipient)) {
    case PDRevision::k20:
      *revision = kPDRevision20;
      break;
    case PDRevision::k30:
      *revision = kPDRevision30;
      break;
    case PDRevision::k31:
      *revision = kPDRevision31;
      break;
    case PDRevision::k32:
      *revision = kPDRevision32;
      break;
    default:
      *revision = 0;
      break;
  }

  return true;
}

bool DBusManager::SetPeripheralDataAccess(brillo::ErrorPtr* err, bool enabled) {
  if (!features_client_) {
    LOG(ERROR) << "Unable to call SetPeripheralDataAccessEnabled";
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_features_client",
                         "Typecd DBusManager failed features_client_ check");
    return false;
  }

  features_client_->SetPeripheralDataAccessEnabled(enabled);
  return true;
}

bool DBusManager::SetPortsUsingDisplays(
    brillo::ErrorPtr* err, const std::vector<uint32_t>& port_nums) {
  if (!port_mgr_) {
    LOG(ERROR) << "PortManager not available for DBusManager";
    brillo::Error::AddTo(err, FROM_HERE, "Typecd", "no_port_manager",
                         "Typecd DBusManager failed port_mgr_ check");
    return false;
  }

  port_mgr_->SetPortsUsingDisplays(port_nums);
  return true;
}

}  // namespace typecd
