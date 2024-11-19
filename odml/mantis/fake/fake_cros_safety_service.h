// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_FAKE_FAKE_CROS_SAFETY_SERVICE_H_
#define ODML_MANTIS_FAKE_FAKE_CROS_SAFETY_SERVICE_H_

#include <memory>
#include <utility>

#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mantis/mock_cloud_safety_session.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom.h"

namespace mantis::fake {

class FakeCrosSafetyService : public cros_safety::mojom::CrosSafetyService {
 public:
  explicit FakeCrosSafetyService(
      raw_ref<mantis::MockCloudSafetySession> session)
      : cloud_safety_session_(session) {}

  void AddReceiver(
      mojo::PendingReceiver<cros_safety::mojom::CrosSafetyService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void CreateOnDeviceSafetySession(
      mojo::PendingReceiver<cros_safety::mojom::OnDeviceSafetySession> session,
      CreateOnDeviceSafetySessionCallback callback) override {
    std::move(callback).Run(
        cros_safety::mojom::GetOnDeviceSafetySessionResult::kOk);
  }

  void CreateCloudSafetySession(
      mojo::PendingReceiver<cros_safety::mojom::CloudSafetySession> session,
      CreateCloudSafetySessionCallback callback) override {
    cloud_safety_session_->AddReceiver(std::move(session));
    std::move(callback).Run(
        cros_safety::mojom::GetCloudSafetySessionResult::kOk);
  }

 private:
  mojo::ReceiverSet<cros_safety::mojom::CrosSafetyService> receiver_set_;
  raw_ref<mantis::MockCloudSafetySession> cloud_safety_session_;
};

class FakeCrosSafetyServiceProviderImpl
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  FakeCrosSafetyServiceProviderImpl(
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager,
      raw_ref<mantis::MockCloudSafetySession> session)
      : receiver_(this), service_impl_(session) {
    service_manager->Register(
        /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
        receiver_.BindNewPipeAndPassRemote());
  }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_impl_.AddReceiver(
        mojo::PendingReceiver<cros_safety::mojom::CrosSafetyService>(
            std::move(receiver)));
  }

  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  // The implementation of cros_safety::mojom::CrosSafetyService.
  FakeCrosSafetyService service_impl_;
};

}  // namespace mantis::fake

#endif  // ODML_MANTIS_FAKE_FAKE_CROS_SAFETY_SERVICE_H_
