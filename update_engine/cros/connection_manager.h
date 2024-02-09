// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_CONNECTION_MANAGER_H_
#define UPDATE_ENGINE_CROS_CONNECTION_MANAGER_H_

#include <memory>
#include <string>

#include <dbus/object_path.h>

#include "update_engine/cros/connection_manager_interface.h"
#include "update_engine/cros/shill_proxy_interface.h"

namespace chromeos_update_engine {

// This class implements the concrete class that talks with the connection
// manager (shill) over DBus.
// TODO(deymo): Remove this class and use ShillProvider from the UpdateManager.
class ConnectionManager : public ConnectionManagerInterface {
 public:
  // Constructs a new ConnectionManager object initialized with the
  // given system state.
  explicit ConnectionManager(ShillProxyInterface* shill_proxy);
  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  ~ConnectionManager() override = default;

  // ConnectionManagerInterface overrides.
  bool GetConnectionProperties(ConnectionType* out_type,
                               bool* out_metered) override;
  bool IsUpdateAllowedOverMetered() const override;
  bool IsAllowedConnectionTypesForUpdateSet() const override;

 private:
  // Returns (via out_path) the default network path, or "/" if there's no
  // network up. Returns true on success.
  bool GetDefaultServicePath(dbus::ObjectPath* out_path);

  bool GetServicePathProperties(const dbus::ObjectPath& path,
                                ConnectionType* out_type,
                                bool* out_metered);

  // The mockable interface to access the shill DBus proxies.
  std::unique_ptr<ShillProxyInterface> shill_proxy_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_CONNECTION_MANAGER_H_
