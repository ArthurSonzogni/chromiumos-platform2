// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_LIB_SIMPLE_MOJO_SERVICE_PROVIDER_H_
#define MOJO_SERVICE_MANAGER_LIB_SIMPLE_MOJO_SERVICE_PROVIDER_H_

#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "mojo_service_manager/lib/mojom/service_manager.mojom.h"

namespace chromeos::mojo_service_manager {

// A simple implementaion of `mojom::ServiceProvider` to provide a mojo service
// to the service manager. It takes a pointer to the implementation of the mojo
// service which will be provided. Just like the mojo::Receiver object, this
// should be held by the implementation to guarantee that the implementation
// won't be accessed after free.
//
// Example:
//   class FooImpl: public mojom::Foo {
//     ...
//     chromeos::mojo_service_manager::SimpleMojoServiceProvider<mojom::Foo>
//       provider_{this};
//   };
template <typename MojoInterfaceType>
class SimpleMojoServiceProvider : public mojom::ServiceProvider {
 public:
  explicit SimpleMojoServiceProvider(MojoInterfaceType* impl)
      : receiver_(this), impl_(impl) {}

  // Register the service to the service manager.
  void Register(
      chromeos::mojo_service_manager::mojom::ServiceManager* service_manager,
      const std::string& service_name) {
    service_manager->Register(service_name,
                              receiver_.BindNewPipeAndPassRemote());
    receiver_.set_disconnect_with_reason_handler(base::BindOnce(
        [](const std::string& service_name, uint32_t error,
           const std::string& message) {
          LOG(ERROR) << "The service provider of " << service_name
                     << " disconnected, error: " << error
                     << ", message: " << message;
        },
        service_name));
  }

 private:
  // mojom::ServiceProvider overrides.
  void Request(mojom::ProcessIdentityPtr identity,
               mojo::ScopedMessagePipeHandle receiver) override {
    service_receiver_set_.Add(
        impl_, mojo::PendingReceiver<MojoInterfaceType>(std::move(receiver)));
  }

  // The receiver to receive requests from the service manager.
  mojo::Receiver<mojom::ServiceProvider> receiver_;
  // The pointer to the implementation of the mojo interface.
  MojoInterfaceType* const impl_;
  // The receiver set to keeps the connections from clients to access the mojo
  // service.
  mojo::ReceiverSet<MojoInterfaceType> service_receiver_set_;
};

}  // namespace chromeos::mojo_service_manager

#endif  // MOJO_SERVICE_MANAGER_LIB_SIMPLE_MOJO_SERVICE_PROVIDER_H_
