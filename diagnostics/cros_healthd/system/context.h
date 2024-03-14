// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_

#include <memory>

#include <brillo/dbus/dbus_connection.h>
#include <brillo/udev/udev_monitor.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace brillo {
class Udev;
};

namespace org {
namespace chromium {
class AttestationProxyInterface;
class bluetoothProxy;
class debugdProxyInterface;
class PowerManagerProxyInterface;
class TpmManagerProxyInterface;
class SpacedProxyInterface;

namespace bluetooth {
class ObjectManagerProxy;

namespace Manager {
class ObjectManagerProxy;
}  // namespace Manager
}  // namespace bluetooth

namespace cras {
class ControlProxyInterface;
}  // namespace cras
}  // namespace chromium

namespace freedesktop {
class fwupdProxyInterface;
}  // namespace freedesktop

class bluezProxy;
}  // namespace org

namespace diagnostics {
class BluezController;
class BluezEventHub;
class CrosConfig;
class FlossController;
class FlossEventHub;
class GroundTruth;
class MojoService;
class PciUtil;
class PowerdAdapter;
class ResourceQueue;
class SystemConfigInterface;
class SystemUtilities;
struct ServiceConfig;

// A context class for holding the helper objects used in cros_healthd, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  Context(mojo::PlatformChannelEndpoint executor_endpoint,
          std::unique_ptr<brillo::UdevMonitor>&& udev_monitor,
          const ServiceConfig& service_config,
          base::OnceClosure shutdown_callback);
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context();

  // Creates an object for accessing |PciUtil| interface.
  virtual std::unique_ptr<PciUtil> CreatePciUtil();

  // Accessors for the various helper objects:
  // Use the object returned by attestation_proxy() to get the attestation
  // information from attestation service.
  org::chromium::AttestationProxyInterface* attestation_proxy() const {
    return attestation_proxy_.get();
  }
  // Use the object returned by cros_config() to query the device's
  // configuration file.
  CrosConfig* cros_config() const { return cros_config_.get(); }
  // Use the object returned by debugd_proxy() to make calls to debugd. Example:
  // cros_healthd calls out to debugd when it needs to collect smart battery
  // metrics like manufacture_date_smart and temperature_smart.
  org::chromium::debugdProxyInterface* debugd_proxy() const {
    return debugd_proxy_.get();
  }
  // Use the object returned by power_manager_proxy() to communicate with power
  // manager daemon through dbus.
  org::chromium::PowerManagerProxyInterface* power_manager_proxy() const {
    return power_manager_proxy_.get();
  }
  // Use the object returned by cras_proxy() to communicate with cras daemon
  // through dbus.
  org::chromium::cras::ControlProxyInterface* cras_proxy() const {
    return cras_proxy_.get();
  }
  // Use the object returned by fwupd_proxy() to communicate with fwupd through
  // dbus.
  org::freedesktop::fwupdProxyInterface* fwupd_proxy() const {
    return fwupd_proxy_.get();
  }
  // Gets GroundTruth to determine conditional features.
  GroundTruth* ground_truth() const { return ground_truth_.get(); }
  // Use the object returned by powerd_adapter() to subscribe to notifications
  // from powerd.
  PowerdAdapter* powerd_adapter() const { return powerd_adapter_.get(); }
  // Use the object returned by udev_monitor() to receive udev events.
  const std::unique_ptr<brillo::UdevMonitor>& udev_monitor() const {
    return udev_monitor_;
  }
  // Use the object returned by system_config() to determine which conditional
  // features a device supports.
  SystemConfigInterface* system_config() const { return system_config_.get(); }
  // Use the interface returned by executor() to make calls to the root-level
  // executor.
  virtual ash::cros_healthd::mojom::Executor* executor() {
    return executor_.get();
  }
  // Use the object returned by system_utils() to access system utilities.
  SystemUtilities* system_utils() const { return system_utils_.get(); }
  // Use the object returned by bluez_controller() to access Bluetooth instances
  // from the Bluez proxy.
  BluezController* bluez_controller() const { return bluez_controller_.get(); }
  // Use the object returned by bluez_event_hub() to subscribe Bluetooth events.
  BluezEventHub* bluez_event_hub() const { return bluez_event_hub_.get(); }
  // Use the object returned by floss_controller() to access Bluetooth instances
  // via Floss proxy.
  FlossController* floss_controller() const { return floss_controller_.get(); }
  // Use the object returned by floss_event_hub() to subscribe Bluetooth events
  // via Floss proxy.
  FlossEventHub* floss_event_hub() const { return floss_event_hub_.get(); }
  // Use the object returned by tpm_manager_proxy() to get the tpm information
  // from tpm manager.
  org::chromium::TpmManagerProxyInterface* tpm_manager_proxy() const {
    return tpm_manager_proxy_.get();
  }
  // Use the object returned by udev() to access udev related interfaces.
  brillo::Udev* udev() const { return udev_.get(); }
  // Get MojoService to access external mojo services.
  MojoService* mojo_service() const { return mojo_service_.get(); }
  // Get a job queue for memory and cpu resource-intensive routines.
  ResourceQueue* memory_cpu_resource_queue() const {
    return memory_cpu_resource_queue_.get();
  }
  // Use the object returned by spaced_proxy() to get the disk information from
  // spaced.
  org::chromium::SpacedProxyInterface* spaced_proxy() const {
    return spaced_proxy_.get();
  }

 protected:
  Context();

 private:
  // Allows MockContext to override the default helper objects.
  friend class MockContext;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus|.
  brillo::DBusConnection connection_;

  // Used to watch udev events.
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;

  // Used to access Bluetooth info and watch Bluetooth events.
  std::unique_ptr<org::bluezProxy> bluez_proxy_;
  std::unique_ptr<org::chromium::bluetooth::Manager::ObjectManagerProxy>
      bluetooth_manager_proxy_;
  std::unique_ptr<org::chromium::bluetooth::ObjectManagerProxy>
      bluetooth_proxy_;

  // Members accessed via the accessor functions defined above.
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::cras::ControlProxyInterface> cras_proxy_;
  std::unique_ptr<CrosConfig> cros_config_;
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  std::unique_ptr<org::freedesktop::fwupdProxyInterface> fwupd_proxy_;
  std::unique_ptr<GroundTruth> ground_truth_;
  std::unique_ptr<MojoService> mojo_service_;
  std::unique_ptr<org::chromium::PowerManagerProxyInterface>
      power_manager_proxy_;
  std::unique_ptr<PowerdAdapter> powerd_adapter_;
  std::unique_ptr<SystemConfigInterface> system_config_;
  mojo::Remote<ash::cros_healthd::mojom::Executor> executor_;
  std::unique_ptr<SystemUtilities> system_utils_;
  std::unique_ptr<BluezController> bluez_controller_;
  std::unique_ptr<BluezEventHub> bluez_event_hub_;
  std::unique_ptr<FlossController> floss_controller_;
  std::unique_ptr<FlossEventHub> floss_event_hub_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
  std::unique_ptr<brillo::Udev> udev_;
  std::unique_ptr<org::chromium::SpacedProxyInterface> spaced_proxy_;

  // The resource queue for jobs using either cpu or memory resources.
  std::unique_ptr<ResourceQueue> memory_cpu_resource_queue_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
