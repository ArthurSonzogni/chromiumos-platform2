// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DBUS_MANAGER_H_
#define TYPECD_DBUS_MANAGER_H_

#include <tuple>
#include <vector>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/errors/error.h>
#include <dbus/typecd/dbus-constants.h>

#include "typecd/chrome_features_service_client.h"
#include "typecd/dbus_adaptors/org.chromium.typecd.h"
#include "typecd/port_manager.h"

namespace typecd {

// DBusManager and PortManager classes include pointers to each other.
// Forward declare PortManager to resolve dependencies during compilation.
class PortManager;

class DBusManager : public org::chromium::typecdAdaptor,
                    public org::chromium::typecdInterface {
 public:
  explicit DBusManager(brillo::dbus_utils::DBusObject* dbus_object);

  virtual void NotifyConnected(DeviceConnectedType type);
  virtual void NotifyCableWarning(CableWarningType type);

  bool GetAltModes(
      brillo::ErrorPtr* err,
      uint32_t port,
      uint32_t recipient,
      std::vector<std::tuple<uint16_t, uint32_t>>* alt_modes) override;
  bool GetCurrentMode(brillo::ErrorPtr* err,
                      uint32_t port,
                      uint32_t* mode) override;
  bool GetIdentity(brillo::ErrorPtr* err,
                   uint32_t port,
                   uint32_t recipient,
                   std::vector<uint32_t>* identity) override;
  bool GetPLD(brillo::ErrorPtr* err,
              uint32_t port,
              std::tuple<uint8_t, uint8_t, uint8_t>* pld) override;
  bool GetPortCount(brillo::ErrorPtr* err, uint32_t* port_count) override;
  bool GetRevision(brillo::ErrorPtr* err,
                   uint32_t port,
                   uint32_t recipient,
                   uint16_t* revision) override;

  bool SetPeripheralDataAccess(brillo::ErrorPtr* err, bool enabled) override;
  bool SetPortsUsingDisplays(brillo::ErrorPtr* err,
                             const std::vector<uint32_t>& port_nums) override;

  void SetFeaturesClient(ChromeFeaturesServiceClient* client) {
    features_client_ = client;
  }
  void SetPortManager(PortManager* mgr) { port_mgr_ = mgr; }

 private:
  ChromeFeaturesServiceClient* features_client_;
  PortManager* port_mgr_;
};

}  // namespace typecd

#endif  // TYPECD_DBUS_MANAGER_H_
