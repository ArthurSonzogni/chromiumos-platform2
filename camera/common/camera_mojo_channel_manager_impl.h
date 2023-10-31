/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
#define CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path_watcher.h>
#include <base/no_destructor.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <iioservice/mojo/cros_sensor_service.mojom.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "camera/mojo/cros_camera_service.mojom.h"
#include "camera/mojo/unguessable_token.mojom.h"
#include "common/sensor_hal_client_impl.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"

namespace cros {

class CameraMojoChannelManagerImpl : public CameraMojoChannelManager {
 public:
  CameraMojoChannelManagerImpl();
  CameraMojoChannelManagerImpl(const CameraMojoChannelManagerImpl&) = delete;
  CameraMojoChannelManagerImpl& operator=(const CameraMojoChannelManagerImpl&) =
      delete;

  ~CameraMojoChannelManagerImpl() override;

  // CameraMojoChannelManager implementations.

  scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner() override;

  mojo::Remote<mojom::CameraAlgorithmOps> CreateCameraAlgorithmOpsRemote(
      const std::string& socket_path, const std::string& pipe_name) override;

  SensorHalClient* GetSensorHalClient() override;

  void RequestServiceFromMojoServiceManager(
      const std::string& service_name,
      mojo::ScopedMessagePipeHandle receiver) override;

  void RegisterServiceToMojoServiceManager(
      const std::string& service_name,
      mojo::PendingRemote<
          chromeos::mojo_service_manager::mojom::ServiceProvider> remote)
      override;

  std::unique_ptr<MojoServiceManagerObserver> CreateMojoServiceManagerObserver(
      const std::string& service_name,
      base::RepeatingClosure on_register_callback,
      base::RepeatingClosure on_unregister_callback) override;

 protected:
  friend class CameraMojoChannelManager;

  // Thread for IPC chores.
  base::Thread ipc_thread_;

 private:
  class MojoServiceManagerObserverImpl;

  void TearDownMojoEnvOnIpcThread();

  chromeos::mojo_service_manager::mojom::ServiceManager*
  GetServiceManagerProxy();

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // TODO(b/151270948): Remove this static variable once we implemnet CrOS
  // specific interface on all camera HALs.
  static CameraMojoChannelManagerImpl* instance_;

  // This lock is to protect |sensor_hal_client_|'s accesses from different
  // threads.
  base::Lock sensor_lock_;
  // The SensorHalClient instance that connects to iioservice for sensors data.
  std::unique_ptr<SensorHalClientImpl> sensor_hal_client_
      GUARDED_BY(sensor_lock_);
};

}  // namespace cros
#endif  // CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
