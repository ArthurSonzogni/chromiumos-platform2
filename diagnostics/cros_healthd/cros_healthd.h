// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Daemon class for cros_healthd.
class CrosHealthd final : public brillo::DBusServiceDaemon {
 public:
  CrosHealthd();
  ~CrosHealthd() override;

 private:
  // brillo::DBusServiceDaemon overrides:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Implementation of the "org.chromium.CrosHealthdInterface" D-Bus interface
  // exposed by the cros_healthd daemon (see constants for the API methods at
  // src/platform2/system_api/dbus/cros_healthd/dbus-constants.h). When
  // |is_chrome| = false, this method will return a unique token that can be
  // used to connect to cros_healthd via mojo. When |is_chrome| = true, the
  // returned string has no meaning.
  std::string BootstrapMojoConnection(const base::ScopedFD& mojo_fd,
                                      bool is_chrome);

  void ShutDownDueToMojoError(const std::string& debug_reason);

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus_|.
  brillo::DBusConnection connection_;
  // Single |dbus_bus_| object used by cros_healthd to initiate the
  // |debugd_proxy_| and |power_manager_proxy_|.
  scoped_refptr<dbus::Bus> dbus_bus_;
  // Use the |debugd_proxy_| to make calls to debugd. Example: cros_healthd
  // calls out to debugd when it needs to collect smart battery metrics like
  // manufacture_date_smart and temperature_smart.
  std::unique_ptr<org::chromium::debugdProxy> debugd_proxy_;
  // Use the |power_manager_proxy_| (owned by |dbus_bus_|) to make calls to
  // power_manager. Example: cros_healthd calls out to power_manager when it
  // needs to collect battery metrics like cycle count.
  dbus::ObjectProxy* power_manager_proxy_;
  // |battery_fetcher_| is responsible for collecting all battery metrics (smart
  // and regular) by using the available D-Bus proxies.
  std::unique_ptr<BatteryFetcher> battery_fetcher_;

  // Production implementation of the CrosHealthdRoutineFactory interface. Will
  // be injected into |routine_service_|.
  CrosHealthdRoutineFactoryImpl routine_factory_impl_;
  // Creates new diagnostic routines and controls existing diagnostic routines.
  std::unique_ptr<CrosHealthdRoutineService> routine_service_;
  // Maintains the Mojo connection with cros_healthd clients.
  std::unique_ptr<CrosHealthdMojoService> mojo_service_;

  // Connects BootstrapMojoConnection with the methods of the D-Bus object
  // exposed by the cros_healthd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthd);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_H_
