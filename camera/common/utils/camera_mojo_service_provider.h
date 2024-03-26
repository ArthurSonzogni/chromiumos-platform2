// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_COMMON_UTILS_CAMERA_MOJO_SERVICE_PROVIDER_H_
#define CAMERA_COMMON_UTILS_CAMERA_MOJO_SERVICE_PROVIDER_H_

#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo_service_manager/lib/connect.h>

#include "cros-camera/common.h"

namespace cros::internal {

template <typename MojoInterfaceType>
class CameraMojoServiceProvider
    : public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  explicit CameraMojoServiceProvider(MojoInterfaceType* impl)
      : receiver_(this), mojo_interface_impl_(impl) {}

  void Register(
      chromeos::mojo_service_manager::mojom::ServiceManager* service_manager,
      const std::string& service_name) {
    LOGF(INFO) << "Register mojo service " << service_name;
    service_manager->Register(service_name,
                              receiver_.BindNewPipeAndPassRemote());
  }

  void Reset() {
    service_receiver_set_.Clear();
    receiver_.reset();
  }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    service_receiver_set_.Add(
        mojo_interface_impl_,
        mojo::PendingReceiver<MojoInterfaceType>(std::move(receiver)));
  }

  // The receiver of ServiceProvider.
  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      receiver_;
  MojoInterfaceType* mojo_interface_impl_;
  mojo::ReceiverSet<MojoInterfaceType> service_receiver_set_;
};

}  // namespace cros::internal

#endif  // CAMERA_COMMON_UTILS_CAMERA_MOJO_SERVICE_PROVIDER_H_
