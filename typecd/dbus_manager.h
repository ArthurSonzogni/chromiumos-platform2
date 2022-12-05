// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DBUS_MANAGER_H_
#define TYPECD_DBUS_MANAGER_H_

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/errors/error.h>
#include <dbus/typecd/dbus-constants.h>

#include "typecd/chrome_features_service_client.h"
#include "typecd/dbus_adaptors/org.chromium.typecd.h"

namespace typecd {

class DBusManager : public org::chromium::typecdAdaptor,
                    public org::chromium::typecdInterface {
 public:
  explicit DBusManager(brillo::dbus_utils::DBusObject* dbus_object);

  virtual void NotifyConnected(DeviceConnectedType type);
  virtual void NotifyCableWarning(CableWarningType type);

  bool SetPeripheralDataAccess(brillo::ErrorPtr* err, bool enabled) override;

  void SetFeaturesClient(ChromeFeaturesServiceClient* client) {
    features_client_ = client;
  }

 private:
  ChromeFeaturesServiceClient* features_client_;
};

}  // namespace typecd

#endif  // TYPECD_DBUS_MANAGER_H_
