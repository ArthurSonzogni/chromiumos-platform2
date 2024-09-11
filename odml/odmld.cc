// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <unistd.h>

#include <memory>

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

#include "odml/coral/service.h"
#include "odml/embedding_model/embedding_model_service.h"
#include "odml/mantis/service.h"
#include "odml/on_device_model/ml/on_device_model_internal.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/utils/odml_shim_loader_impl.h"

namespace {

class OnDeviceModelServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  OnDeviceModelServiceProviderImpl(
      raw_ref<MetricsLibrary> metrics,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager)
      : receiver_(this),
        service_impl_(
            metrics,
            raw_ref(shim_loader_),
            ml::GetOnDeviceModelInternalImpl(metrics, raw_ref(shim_loader_))) {
    service_manager->Register(
        /*service_name=*/chromeos::mojo_services::kCrosOdmlService,
        receiver_.BindNewPipeAndPassRemote());
  }

  raw_ref<on_device_model::OnDeviceModelService> service() {
    return raw_ref(service_impl_);
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

  // The odml_shim loader.
  odml::OdmlShimLoaderImpl shim_loader_;
  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of on_device_model::mojom::OnDeviceModelPlatformService.
  on_device_model::OnDeviceModelService service_impl_;
};

class EmbeddingModelServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  explicit EmbeddingModelServiceProviderImpl(
      raw_ref<MetricsLibrary> metrics,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager)
      : receiver_(this), service_impl_(metrics) {
    service_manager->Register(
        chromeos::mojo_services::kCrosEmbeddingModelService,
        receiver_.BindNewPipeAndPassRemote());
  }

  raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService> service() {
    return raw_ref(service_impl_);
  }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_impl_.AddReceiver(
        mojo::PendingReceiver<
            embedding_model::mojom::OnDeviceEmbeddingModelService>(
            std::move(receiver)));
  }

  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of
  // embedding_model::mojom::OnDeviceEmbeddingModelService.
  embedding_model::EmbeddingModelService service_impl_;
};

class CoralServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  CoralServiceProviderImpl(
      raw_ref<MetricsLibrary> metrics,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager,
      raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
          on_device_model_service,
      raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
          embedding_model_service)
      : receiver_(this),
        service_impl_(on_device_model_service, embedding_model_service) {
    service_manager->Register(chromeos::mojo_services::kCrosCoralService,
                              receiver_.BindNewPipeAndPassRemote());
  }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_impl_.AddReceiver(
        mojo::PendingReceiver<coral::mojom::CoralService>(std::move(receiver)));
  }

  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of coral::mojom::CoralService.
  coral::CoralService service_impl_;
};

class MantisServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  MantisServiceProviderImpl(
      raw_ref<MetricsLibrary> metrics,
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager)
      : metrics_(metrics),
        receiver_(this),
        service_impl_(raw_ref(shim_loader_)) {
    service_manager->Register(
        /*service_name=*/chromeos::mojo_services::kCrosMantisService,
        receiver_.BindNewPipeAndPassRemote());
  }

  raw_ref<mantis::MantisService> service() { return raw_ref(service_impl_); }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_impl_.AddReceiver(
        mojo::PendingReceiver<mantis::mojom::MantisService>(
            std::move(receiver)));
  }

  // The metrics lib.
  raw_ref<MetricsLibrary> metrics_;
  // The odml_shim loader.
  odml::OdmlShimLoaderImpl shim_loader_;
  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of mantis::mojom::MantisService.
  mantis::MantisService service_impl_;
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

    on_device_model_service_provider_impl_ =
        std::make_unique<OnDeviceModelServiceProviderImpl>(raw_ref(metrics_),
                                                           service_manager_);
    embedding_model_service_provider_impl_ =
        std::make_unique<EmbeddingModelServiceProviderImpl>(raw_ref(metrics_),
                                                            service_manager_);
    coral_service_provider_impl_ = std::make_unique<CoralServiceProviderImpl>(
        raw_ref(metrics_), service_manager_,
        on_device_model_service_provider_impl_->service(),
        embedding_model_service_provider_impl_->service());

    mantis_service_provider_impl_ = std::make_unique<MantisServiceProviderImpl>(
        raw_ref(metrics_), service_manager_);
    return 0;
  }

 private:
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;

  // The metrics lib. Should be destructed after both service providers.
  MetricsLibrary metrics_;

  std::unique_ptr<OnDeviceModelServiceProviderImpl>
      on_device_model_service_provider_impl_;

  std::unique_ptr<EmbeddingModelServiceProviderImpl>
      embedding_model_service_provider_impl_;

  std::unique_ptr<CoralServiceProviderImpl> coral_service_provider_impl_;

  std::unique_ptr<MantisServiceProviderImpl> mantis_service_provider_impl_;

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
