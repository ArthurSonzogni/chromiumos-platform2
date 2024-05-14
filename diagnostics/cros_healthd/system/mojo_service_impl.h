// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <camera/mojo/camera_diagnostics.mojom.h>
#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/external/cros_healthd_internal.mojom.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/external/network_health.mojom.h"

namespace diagnostics {

// The implementation of MojoService.
class MojoServiceImpl : public MojoService {
 public:
  MojoServiceImpl(const MojoServiceImpl&) = delete;
  MojoServiceImpl& operator=(const MojoServiceImpl&) = delete;
  ~MojoServiceImpl() override;

  // Creates an instance with all the services initialized.
  static std::unique_ptr<MojoServiceImpl> Create(
      base::OnceClosure shutdown_callback);

  // MojoService overrides.
  chromeos::mojo_service_manager::mojom::ServiceManager* GetServiceManager()
      override;
  ash::cros_healthd::internal::mojom::ChromiumDataCollector*
  GetChromiumDataCollector() override;
  chromeos::network_health::mojom::NetworkHealthService* GetNetworkHealth()
      override;
  chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines*
  GetNetworkDiagnosticsRoutines() override;
  cros::mojom::SensorService* GetSensorService() override;
  cros::mojom::SensorDevice* GetSensorDevice(int32_t device_id) override;
  cros::camera_diag::mojom::CameraDiagnostics* GetCameraDiagnostics() override;

 protected:
  MojoServiceImpl();

  // Getters for subclass to modify the value.
  mojo::Remote<ash::cros_healthd::internal::mojom::ChromiumDataCollector>&
  chromium_data_collector() {
    return chromium_data_collector_;
  }
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
  service_manager() {
    return service_manager_;
  }
  mojo::Remote<cros::mojom::SensorService>& sensor_service() {
    return sensor_service_;
  }
  mojo::Remote<chromeos::network_health::mojom::NetworkHealthService>&
  network_health_service() {
    return network_health_service_;
  }
  mojo::Remote<
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>&
  network_diagnostics_routines() {
    return network_diagnostics_routines_;
  }
  mojo::Remote<cros::camera_diag::mojom::CameraDiagnostics>&
  camera_diagnostics() {
    return camera_diagnostics_;
  }

 private:
  // Requests the service from service manager. This will also setup the
  // disconnect handler which will reconnect after service disconnected.
  // |remote| must be a member of this class. |delay| is used for adding delay
  // when reconnecting.
  template <typename InterfaceType>
  void RequestService(const std::string& service_name,
                      mojo::Remote<InterfaceType>& remote,
                      const base::TimeDelta& delay);

  // Sends the service request to the service manager.
  template <typename InterfaceType>
  void SendServiceRequest(
      const std::string& service_name,
      mojo::PendingReceiver<InterfaceType> pending_receiver);

  // Handles services disconnection.
  template <typename InterfaceType>
  void OnServiceDisconnect(const std::string& service_name,
                           mojo::Remote<InterfaceType>& remote,
                           uint32_t error,
                           const std::string& message);

  // Bind the sensor device if it is not bound.
  void BindSensorDeviceRemoteIfNeeded(int32_t device_id);

  // Mojo remotes or adaptors to access mojo interfaces.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  mojo::Remote<ash::cros_healthd::internal::mojom::ChromiumDataCollector>
      chromium_data_collector_;
  mojo::Remote<chromeos::network_health::mojom::NetworkHealthService>
      network_health_service_;
  mojo::Remote<chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
      network_diagnostics_routines_;
  mojo::Remote<cros::mojom::SensorService> sensor_service_;
  std::map<int32_t, mojo::Remote<cros::mojom::SensorDevice>> sensor_devices_;
  mojo::Remote<cros::camera_diag::mojom::CameraDiagnostics> camera_diagnostics_;

  // Must be the last class member.
  base::WeakPtrFactory<MojoServiceImpl> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
