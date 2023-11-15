/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_mojo_channel_manager_impl.h"

#include <grp.h>

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/threading/sequence_bound.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo_service_manager/lib/connect.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

namespace cros {

using chromeos::mojo_service_manager::mojom::ErrorOrServiceState;
using chromeos::mojo_service_manager::mojom::ServiceState;

MojoServiceManagerObserver::~MojoServiceManagerObserver() {}

class CameraMojoChannelManagerImpl::MojoServiceManagerObserverImpl
    : public MojoServiceManagerObserver {
 public:
  MojoServiceManagerObserverImpl(
      chromeos::mojo_service_manager::mojom::ServiceManager*
          service_manager_proxy,
      const std::string& service_name,
      base::RepeatingClosure on_register_callback,
      base::RepeatingClosure on_unregister_callback)
      : helper_(CameraMojoChannelManager::GetInstance()->GetIpcTaskRunner(),
                service_manager_proxy,
                service_name,
                std::move(on_register_callback),
                std::move(on_unregister_callback)) {}
  ~MojoServiceManagerObserverImpl() override = default;

 private:
  class ObserverHelper
      : public chromeos::mojo_service_manager::mojom::ServiceObserver {
   public:
    ObserverHelper(chromeos::mojo_service_manager::mojom::ServiceManager*
                       service_manager_proxy,
                   const std::string& service_name,
                   base::RepeatingClosure on_register_callback,
                   base::RepeatingClosure on_unregister_callback)
        : service_manager_proxy_(service_manager_proxy),
          service_name_(service_name),
          on_register_callback_(std::move(on_register_callback)),
          on_unregister_callback_(std::move(on_unregister_callback)) {
      // It is always safe here since |service_manager_proxy_| points to a
      // NoDestructor object.
      service_manager_proxy_->AddServiceObserver(
          observer_receiver_.BindNewPipeAndPassRemote());
      service_manager_proxy_->Query(
          service_name_, base::BindOnce(&ObserverHelper::QueryCallback,
                                        weak_ptr_factory_.GetWeakPtr()));
    }

    ~ObserverHelper() override = default;

   private:
    void OnServiceEvent(
        chromeos::mojo_service_manager::mojom::ServiceEventPtr event) override {
      if (event->service_name != service_name_) {
        return;
      }
      switch (event->type) {
        case chromeos::mojo_service_manager::mojom::ServiceEvent::Type::
            kRegistered:
          on_register_callback_.Run();
          return;

        case chromeos::mojo_service_manager::mojom::ServiceEvent::Type::
            kUnRegistered:
          on_unregister_callback_.Run();
          return;

        case chromeos::mojo_service_manager::mojom::ServiceEvent::Type::
            kDefaultValue:
          return;
      }
    }

    void QueryCallback(
        chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr result) {
      switch (result->which()) {
        case ErrorOrServiceState::Tag::kState:
          switch (result->get_state()->which()) {
            case ServiceState::Tag::kRegisteredState:
              on_register_callback_.Run();
              break;

            case ServiceState::Tag::kUnregisteredState:
              break;

            case ServiceState::Tag::kDefaultType:
              break;
          }
          break;

        case ErrorOrServiceState::Tag::kError:
          LOG(ERROR) << "Error code: " << result->get_error()->code
                     << ", message: " << result->get_error()->message;
          break;

        case ErrorOrServiceState::Tag::kDefaultType:
          LOG(ERROR) << "Unknown type: " << result->get_default_type();
          break;
      }
    }

    chromeos::mojo_service_manager::mojom::ServiceManager*
        service_manager_proxy_;

    std::string service_name_;

    base::RepeatingClosure on_register_callback_;

    base::RepeatingClosure on_unregister_callback_;

    mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceObserver>
        observer_receiver_{this};

    base::WeakPtrFactory<ObserverHelper> weak_ptr_factory_{this};
  };

  base::SequenceBound<ObserverHelper> helper_;
};

// static
CameraMojoChannelManagerImpl* CameraMojoChannelManagerImpl::instance_ = nullptr;

CameraMojoChannelManagerImpl::CameraMojoChannelManagerImpl()
    : ipc_thread_("MojoIpcThread") {
  instance_ = this;
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOGF(ERROR) << "Failed to start IPC Thread";
    return;
  }
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      GetIpcTaskRunner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
}

CameraMojoChannelManagerImpl::~CameraMojoChannelManagerImpl() {
  if (ipc_thread_.IsRunning()) {
    base::AutoLock lock(sensor_lock_);
    sensor_hal_client_.reset();
    GetIpcTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &CameraMojoChannelManagerImpl::TearDownMojoEnvOnIpcThread,
            base::Unretained(this)));
    ipc_thread_.Stop();
  }
}

// static
CameraMojoChannelManagerToken* CameraMojoChannelManagerToken::CreateInstance() {
  return new CameraMojoChannelManagerImpl();
}

// static
CameraMojoChannelManager* CameraMojoChannelManager::GetInstance() {
  DCHECK(CameraMojoChannelManagerImpl::instance_ != nullptr);
  return CameraMojoChannelManagerImpl::instance_;
}

scoped_refptr<base::SingleThreadTaskRunner>
CameraMojoChannelManagerImpl::GetIpcTaskRunner() {
  CHECK(ipc_thread_.task_runner());
  return ipc_thread_.task_runner();
}

mojo::Remote<mojom::CameraAlgorithmOps>
CameraMojoChannelManagerImpl::CreateCameraAlgorithmOpsRemote(
    const std::string& socket_path, const std::string& pipe_name) {
  mojo::ScopedMessagePipeHandle parent_pipe;
  mojo::Remote<mojom::CameraAlgorithmOps> algorithm_ops;

  base::FilePath socket_file_path(socket_path);
  MojoResult result = cros::CreateMojoChannelToChildByUnixDomainSocket(
      socket_file_path, &parent_pipe, pipe_name);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to "
                  << socket_file_path.value();
    return mojo::Remote<mojom::CameraAlgorithmOps>();
  }

  algorithm_ops.Bind(mojo::PendingRemote<mojom::CameraAlgorithmOps>(
      std::move(parent_pipe), 0u));

  LOGF(INFO) << "Connected to CameraAlgorithmOps";

  return algorithm_ops;
}

SensorHalClient* CameraMojoChannelManagerImpl::GetSensorHalClient() {
  base::AutoLock lock(sensor_lock_);
  if (!sensor_hal_client_) {
    sensor_hal_client_ = std::make_unique<SensorHalClientImpl>(this);
  }
  return sensor_hal_client_.get();
}

void CameraMojoChannelManagerImpl::RequestServiceFromMojoServiceManager(
    const std::string& service_name, mojo::ScopedMessagePipeHandle receiver) {
  DCHECK(GetIpcTaskRunner()->BelongsToCurrentThread());
  GetServiceManagerProxy()->Request(service_name, std::nullopt,
                                    std::move(receiver));
}

void CameraMojoChannelManagerImpl::TearDownMojoEnvOnIpcThread() {
  DCHECK(GetIpcTaskRunner()->BelongsToCurrentThread());
  ipc_support_.reset();
}

chromeos::mojo_service_manager::mojom::ServiceManager*
CameraMojoChannelManagerImpl::GetServiceManagerProxy() {
  DCHECK(GetIpcTaskRunner()->BelongsToCurrentThread());
  static const base::NoDestructor<
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
      remote(chromeos::mojo_service_manager::ConnectToMojoServiceManager());
  CHECK(remote->is_bound()) << "Failed to connect to mojo service manager.";
  return remote->get();
}

void CameraMojoChannelManagerImpl::RegisterServiceToMojoServiceManager(
    const std::string& service_name,
    mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceProvider>
        remote) {
  DCHECK(GetIpcTaskRunner()->BelongsToCurrentThread());
  GetServiceManagerProxy()->Register(service_name, std::move(remote));
}

std::unique_ptr<MojoServiceManagerObserver>
CameraMojoChannelManagerImpl::CreateMojoServiceManagerObserver(
    const std::string& service_name,
    base::RepeatingClosure on_register_callback,
    base::RepeatingClosure on_unregister_callback) {
  return base::WrapUnique<MojoServiceManagerObserver>(
      new MojoServiceManagerObserverImpl(GetServiceManagerProxy(), service_name,
                                         std::move(on_register_callback),
                                         std::move(on_unregister_callback)));
}

void CameraMojoChannelManagerImpl::IsServiceRegistered(
    const std::string& service_name, base::OnceCallback<void(bool)> callback) {
  GetServiceManagerProxy()->Query(
      service_name,
      base::BindOnce(&CameraMojoChannelManagerImpl::QueryCallback,
                     base::Unretained(this), std::move(callback)));
}

void CameraMojoChannelManagerImpl::QueryCallback(
    base::OnceCallback<void(bool)> callback,
    chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr result) {
  if (result->which() == ErrorOrServiceState::Tag::kState &&
      result->get_state()->which() == ServiceState::Tag::kRegisteredState) {
    std::move(callback).Run(true);
    return;
  }
  std::move(callback).Run(false);
}

}  // namespace cros
