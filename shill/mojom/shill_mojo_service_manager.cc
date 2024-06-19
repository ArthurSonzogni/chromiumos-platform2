// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/shill_mojo_service_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/memory/weak_ptr.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/thread.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/enum_utils.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>
#include <mojo_service_manager/lib/simple_mojo_service_provider.h>

#include "shill/manager.h"
#include "shill/mojom/mojo_passpoint_service.h"
#include "shill/mojom/mojo_portal_service.h"
#include "shill/wifi/wifi_provider.h"

namespace shill {
namespace {
// The delay of reconnecting when disconnected from the service.
constexpr base::TimeDelta kReconnectDelay = base::Seconds(1);
}  // namespace

class ShillMojoServiceManagerImpl : public ShillMojoServiceManager {
 public:
  explicit ShillMojoServiceManagerImpl(Manager* manager);
  ShillMojoServiceManagerImpl(const ShillMojoServiceManagerImpl&) = delete;
  ShillMojoServiceManagerImpl& operator=(const ShillMojoServiceManagerImpl&) =
      delete;

  ~ShillMojoServiceManagerImpl() override;

 private:
  // Bind the provider to the service manager and register our services.
  void ConnectAndRegister();

  // Called when the manager disconnects.
  void OnManagerDisconnected(uint32_t error, const std::string& message);

  // Thread for running IPC requests.
  base::Thread ipc_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // Mojo service manager.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;

  // Used to register the Passpoint service as an observer of Passpoint
  // credentials events.
  Manager* manager_;

  // Passpoint Mojo service implementation.
  MojoPasspointService passpoint_service_;
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      MojoPasspointService>
      passpoint_service_provider_{&passpoint_service_};

  // Portal Mojo service implementation.
  MojoPortalService portal_service_;
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<MojoPortalService>
      portal_service_provider_{&portal_service_};

  // Must be the last class member.
  base::WeakPtrFactory<ShillMojoServiceManagerImpl> weak_ptr_factory_{this};
};

ShillMojoServiceManagerImpl::ShillMojoServiceManagerImpl(Manager* manager)
    : ipc_thread_("Mojo IPC"),
      manager_(manager),
      passpoint_service_(manager),
      portal_service_(manager->network_manager()) {
  // TODO(b/266150324): investigate if we really need a separate IO thread.
  ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));

  // Initialize Mojo for the whole process.
  // Note: this must be called only once per process. It works because
  // ShillMojoServiceManager is only created once by DaemonTask on Shill
  // startup.
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  WiFiProvider* provider = manager_->wifi_provider();
  CHECK(provider);
  provider->AddPasspointCredentialsObserver(&passpoint_service_);

  ConnectAndRegister();
}

ShillMojoServiceManagerImpl::~ShillMojoServiceManagerImpl() {
  WiFiProvider* provider = manager_->wifi_provider();
  CHECK(provider);
  provider->RemovePasspointCredentialsObserver(&passpoint_service_);

  if (ipc_thread_.IsRunning()) {
    ipc_support_.reset();
    ipc_thread_.Stop();
  }
}

void ShillMojoServiceManagerImpl::ConnectAndRegister() {
  mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceManager>
      pending_remote =
          chromeos::mojo_service_manager::ConnectToMojoServiceManager();
  if (!pending_remote.is_valid()) {
    LOG(ERROR) << "Mojo service manager is not available.";
    return;
  }
  service_manager_.Bind(std::move(pending_remote));
  service_manager_.set_disconnect_with_reason_handler(
      base::BindOnce(&ShillMojoServiceManagerImpl::OnManagerDisconnected,
                     weak_ptr_factory_.GetWeakPtr()));

  // Register the service providers.
  passpoint_service_provider_.Register(
      service_manager_.get(), chromeos::mojo_services::kCrosPasspointService);
  portal_service_provider_.Register(
      service_manager_.get(), chromeos::mojo_services::kCrosPortalService);
}

void ShillMojoServiceManagerImpl::OnManagerDisconnected(
    uint32_t error, const std::string& message) {
  if (error == 0) {
    LOG(WARNING)
        << "Disconnected from service manager, scheduling reconnection";
    // The remote service probably restarted, try to reconnect.
    // TODO(b/266150324): implement a backoff or a max reconnection logic.
    service_manager_.reset();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ShillMojoServiceManagerImpl::ConnectAndRegister,
                       weak_ptr_factory_.GetWeakPtr()),
        kReconnectDelay);
    return;
  }

  const auto error_enum = mojo::ConvertIntToMojoEnum<
      chromeos::mojo_service_manager::mojom::ErrorCode>(
      static_cast<int32_t>(error));
  if (error_enum) {
    LOG(ERROR) << "Service manager disconnected with error "
               << error_enum.value() << ", message: " << message;
  } else {
    LOG(ERROR) << "Service manager disconnected with error " << error
               << ", message: " << message;
  }
}

// static
std::unique_ptr<ShillMojoServiceManager> ShillMojoServiceManager::Create(
    Manager* manager) {
  return std::make_unique<ShillMojoServiceManagerImpl>(manager);
}

}  // namespace shill
