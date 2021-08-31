// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/tick_clock.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/udev/udev_monitor.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "cras/dbus-proxies.h"
#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/cros_healthd/executor/executor_adapter.h"
#include "diagnostics/cros_healthd/network/network_health_adapter.h"
#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"
#include "diagnostics/cros_healthd/system/system_utilities.h"
#include "diagnostics/cros_healthd/system/udev_interface.h"

namespace org {
namespace chromium {
class debugdProxyInterface;
}  // namespace chromium
}  // namespace org

namespace diagnostics {

// A context class for holding the helper objects used in cros_healthd, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context();

  static std::unique_ptr<Context> Create(
      mojo::PlatformChannelEndpoint endpoint,
      std::unique_ptr<brillo::UdevMonitor>&& udev_monitor);

  // Initializes all helper objects in the context. Returns true on success.
  // Must be called before any of the accessors are used.
  virtual bool Initialize();

  // Accessors for the various helper objects:

  // Use the object returned by bluetooth_client() to subscribe to notifications
  // for D-Bus objects representing Bluetooth adapters and devices.
  BluetoothClient* bluetooth_client() const;
  // Use the object returned by cros_config() to query the device's
  // configuration file.
  brillo::CrosConfigInterface* cros_config() const;
  // Use the object returned by debugd_proxy() to make calls to debugd. Example:
  // cros_healthd calls out to debugd when it needs to collect smart battery
  // metrics like manufacture_date_smart and temperature_smart.
  org::chromium::debugdProxyInterface* debugd_proxy() const;
  // Use the object returned by cras_proxy() to communicate with cras daemon
  // through dbus.
  org::chromium::cras::ControlProxyInterface* cras_proxy() const;
  // Use the object returned by debugd_adapter() to make calls to debugd.
  // Example: cros_healthd calls out to debugd with async callbacks when it
  // needs to trigger nvme self-test or collect data like progress info.
  DebugdAdapter* debugd_adapter() const;
  // Use the object returned by network_health_adapter() to make requests to the
  // NetworkHealthService. Example: cros_healthd calls out to the
  // NetworkHealthService to get network telemetry data.
  NetworkHealthAdapter* network_health_adapter() const;
  // Use the object returned by network_diagnostics_adapter() to make calls to
  // the NetworkDiagnosticsRoutines interface implemented by the browser.
  // Example: cros_healthd calls out to the NetworkDiagnosticsRoutines interface
  // with async callbacks when it needs to run network diagnostics.
  NetworkDiagnosticsAdapter* network_diagnostics_adapter() const;
  // Use the object returned by powerd_adapter() to subscribe to notifications
  // from powerd.
  PowerdAdapter* powerd_adapter() const;
  // Use the object returned by root_dir() to determine the root directory of
  // the system.
  const base::FilePath& root_dir() const;
  // Use the object returned by udev_monitor() to receive udev events.
  const std::unique_ptr<brillo::UdevMonitor>& udev_monitor() const;
  // Use the object returned by system_config() to determine which conditional
  // features a device supports.
  SystemConfigInterface* system_config() const;
  // Use the object returned by executor() to make calls to the root-level
  // executor.
  ExecutorAdapter* executor() const;
  // Use the object returned by system_utils() to access system utilities.
  SystemUtilities* system_utils() const;
  // Use the object returned by tick_clock() to track the passage of time.
  base::TickClock* tick_clock() const;
  // Return current time.
  virtual const base::Time time() const;
  // Use the object returned by udev() to access udev related interfaces.
  UdevInterface* udev() const;

 private:
  Context();

 private:
  // Allows MockContext to override the default helper objects.
  friend class MockContext;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus|.
  brillo::DBusConnection connection_;

  // Used by this object to initialize the SystemConfig. Used for reading
  // cros_config properties to determine device feature support.
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;

  // Used to watch udev events.
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;

  // Members accessed via the accessor functions defined above.
  std::unique_ptr<BluetoothClient> bluetooth_client_;
  std::unique_ptr<org::chromium::cras::ControlProxyInterface> cras_proxy_;
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  std::unique_ptr<DebugdAdapter> debugd_adapter_;
  std::unique_ptr<NetworkHealthAdapter> network_health_adapter_;
  std::unique_ptr<NetworkDiagnosticsAdapter> network_diagnostics_adapter_;
  std::unique_ptr<PowerdAdapter> powerd_adapter_;
  std::unique_ptr<SystemConfigInterface> system_config_;
  std::unique_ptr<ExecutorAdapter> executor_;
  std::unique_ptr<SystemUtilities> system_utils_;
  std::unique_ptr<base::TickClock> tick_clock_;
  std::unique_ptr<UdevInterface> udev_;
  base::FilePath root_dir_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
