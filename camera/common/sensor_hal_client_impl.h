/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_SENSOR_HAL_CLIENT_IMPL_H_
#define CAMERA_COMMON_SENSOR_HAL_CLIENT_IMPL_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/sequence_bound.h>
#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "common/sensor_reader.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"
#include "cros-camera/sensor_hal_client.h"

namespace cros {

class CameraMojoChannelManager;

class SensorHalClientImpl : public SensorHalClient {
 public:
  explicit SensorHalClientImpl(CameraMojoChannelManager* mojo_manager);
  SensorHalClientImpl(const SensorHalClientImpl&) = delete;
  SensorHalClientImpl& operator=(const SensorHalClientImpl&) = delete;

  ~SensorHalClientImpl() override;

  // SensorHalClient implementations.
  bool HasDevice(DeviceType type, Location location) override;
  bool RegisterSamplesObserver(DeviceType type,
                               Location location,
                               double frequency,
                               SamplesObserver* samples_observer) override;
  void UnregisterSamplesObserver(SamplesObserver* samples_observer) override;

 private:
  // IPCBridge wraps all the IPC-related calls. Most of its methods should/will
  // be run on IPC thread.
  class IPCBridge : public mojom::SensorServiceNewDevicesObserver {
   public:
    explicit IPCBridge(CameraMojoChannelManager* mojo_manager);

    // It should only be triggered on IPC thread to ensure thread-safety.
    ~IPCBridge() override;

    void HasDevice(mojom::DeviceType type,
                   Location location,
                   base::OnceCallback<void(bool)> callback);
    void RegisterSamplesObserver(mojom::DeviceType type,
                                 Location location,
                                 double frequency,
                                 SamplesObserver* samples_observer,
                                 base::OnceCallback<void(bool)> callback);
    void UnregisterSamplesObserver(SamplesObserver* samples_observer);

    void SetUpChannel(mojo::PendingRemote<mojom::SensorService> pending_remote);

    // SensorServiceNewDevicesObserver Mojo interface implementation.
    void OnNewDeviceAdded(int32_t iio_device_id,
                          const std::vector<mojom::DeviceType>& types) override;
    void OnDeviceRemoved(int32_t iio_device_id) override;

    bool IsReady() { return sensor_service_remote_.is_bound(); }

    // Gets a weak pointer of the IPCBridge. This method can be called on
    // non-IPC thread.
    base::WeakPtr<IPCBridge> GetWeakPtr();

   private:
    struct DeviceData {
      bool ignored = false;

      std::vector<mojom::DeviceType> types;
      std::optional<Location> location;
      std::optional<double> scale;

      // Temporarily stores the remote, waiting for its attributes information.
      // It'll be passed to SensorDevice's constructor as an argument after all
      // information is collected, if this device is needed.
      mojo::Remote<mojom::SensorDevice> remote;
    };

    struct DeviceQueryInfo {
      mojom::DeviceType type;
      Location location;
      base::OnceCallback<void(bool)> callback;
    };

    struct ReaderData {
      int32_t iio_device_id;
      mojom::DeviceType type;
      double frequency;
      std::unique_ptr<SensorReader> sensor_reader;
    };

    // Used as MojoServiceManagerObserver's OnRegisterCallback.
    void RequestService();
    void OnUnregisterCallback();

    void OnDeviceQueryTimedOut(uint32_t info_id);

    void RegisterDevice(int32_t iio_device_id,
                        const std::vector<mojom::DeviceType>& types);

    void GetAllDeviceIdsCallback(
        const base::flat_map<int32_t, std::vector<mojom::DeviceType>>&
            iio_device_ids_types);

    mojo::Remote<mojom::SensorDevice> GetSensorDeviceRemote(
        int32_t iio_device_id);

    void GetAttributesCallback(
        int32_t iio_device_id,
        const std::vector<std::string> attr_names,
        const std::vector<std::optional<std::string>>& values);

    void IgnoreDevice(int32_t iio_device_id);
    // Return true if all devices of |type| are initialized and attributes are
    // ready. We can further process the queries of |type|.
    bool AreAllDevicesOfTypeInitialized(mojom::DeviceType type);

    void RunDeviceQueriesForType(mojom::DeviceType type);

    bool HasDeviceInternal(mojom::DeviceType type, Location location);

    void ResetSensorService();

    void OnSensorServiceDisconnect();
    void OnNewDevicesObserverDisconnect();

    CameraMojoChannelManager* mojo_manager_;

    std::unique_ptr<MojoServiceManagerObserver> mojo_service_manager_observer_;

    mojo::Remote<mojom::SensorService> sensor_service_remote_;

    // The Mojo channel to get notified when new devices are added to IIO
    // Service.
    mojo::Receiver<mojom::SensorServiceNewDevicesObserver>
        new_devices_observer_{this};

    uint32_t device_query_info_counter_ = 0;
    // First is the info id, second is the pending HasDevice query.
    std::map<uint32_t, DeviceQueryInfo> device_queries_info_;

    // Maps from DeviceType and Location to id.
    std::map<mojom::DeviceType, std::map<Location, int32_t>> device_maps_;

    bool devices_retrieved_ = false;

    // First is iio_device_id, second is the device's attributes and Mojo
    // remote.
    std::map<int32_t, DeviceData> devices_;

    // First is the observer's pointer, second is the specific sensor reader.
    std::map<SamplesObserver*, ReaderData> readers_;

    base::WeakPtrFactory<IPCBridge> weak_ptr_factory_{this};
  };

  std::unique_ptr<CancellationRelay> cancellation_relay_;

  // The instance which deals with the IPC-related calls. It's bound on the IPC
  // thread.
  base::SequenceBound<IPCBridge> ipc_bridge_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_SENSOR_HAL_CLIENT_IMPL_H_
