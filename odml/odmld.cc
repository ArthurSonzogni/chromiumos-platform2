// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/memory/raw_ref.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <metrics/metrics_library.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include "odml/on_device_model/on_device_model_factory_impl.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/utils/odml_shim_loader_impl.h"

namespace {

class ServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  explicit ServiceProviderImpl(
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager)
      : receiver_(this),
        service_impl_(
            raw_ref(factory_impl_), raw_ref(metrics_), raw_ref(shim_loader_)) {
    service_manager->Register(
        /*service_name=*/chromeos::mojo_services::kCrosOdmlService,
        receiver_.BindNewPipeAndPassRemote());
  }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_impl_.AddReceiver(
        mojo::PendingReceiver<
            on_device_model::mojom::OnDeviceModelPlatformService>(
            std::move(receiver)));
  }

  // The model factory.
  on_device_model::OndeviceModelFactoryImpl factory_impl_;
  // The metrics lib.
  MetricsLibrary metrics_;
  // The odml_shim loader.
  odml::OdmlShimLoaderImpl shim_loader_;
  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of on_device_model::mojom::OnDeviceModelPlatformService.
  on_device_model::OnDeviceModelService service_impl_;
};

class Daemon : public brillo::Daemon {
 public:
  Daemon() = default;
  ~Daemon() = default;

 protected:
  // brillo::DBusDaemon:
  int OnInit() override {
    mojo::core::Init();

    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        base::SingleThreadTaskRunner::GetCurrentDefault(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

    auto service_manager_remote =
        chromeos::mojo_service_manager::ConnectToMojoServiceManager();

    if (!service_manager_remote) {
      LOG(ERROR) << "Failed to connect to Mojo Service Manager";
      return -1;
    }

    service_manager_.Bind(std::move(service_manager_remote));
    service_manager_.set_disconnect_with_reason_handler(
        base::BindOnce([](uint32_t error, const std::string& message) {
          LOG(INFO) << "Disconnected from mojo service manager (the mojo "
                       "broker process). Error: "
                    << error << ", message: " << message
                    << ". Shutdown and wait for respawn.";
        }));

    service_provider_impl_ =
        std::make_unique<ServiceProviderImpl>(service_manager_);
    return 0;
  }

 private:
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;

  std::unique_ptr<ServiceProviderImpl> service_provider_impl_;

  // Must be last class member.
  base::WeakPtrFactory<Daemon> weak_factory_{this};
};

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  brillo::FlagHelper::Init(argc, argv, "ChromeOS ODML Daemon");
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  Daemon daemon;
  return daemon.Run();
}
