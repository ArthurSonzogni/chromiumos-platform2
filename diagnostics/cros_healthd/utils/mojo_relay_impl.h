// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_IMPL_H_

#include <memory>
#include <utility>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "diagnostics/cros_healthd/utils/mojo_relay.h"

namespace diagnostics {

template <typename Interface>
class MojoRelayImpl : public MojoRelay<Interface> {
 public:
  MojoRelayImpl() {
    pending_receiver_ = proxy_remote_.BindNewPipeAndPassReceiver();
  }
  ~MojoRelayImpl() override = default;
  MojoRelayImpl(const MojoRelayImpl&) = delete;
  MojoRelayImpl& operator=(const MojoRelayImpl&) = delete;

 public:
  typename Interface::Proxy_* Get() override { return proxy_remote_.get(); }

  bool IsBound() override { return real_remote_.is_bound(); }

  void Bind(mojo::PendingRemote<Interface> pending_remote) override {
    real_remote_.Bind(std::move(pending_remote));
    proxy_receiver_ = std::make_unique<mojo::Receiver<Interface>>(
        real_remote_.get(), std::move(pending_receiver_));
  }

 private:
  // Used to queue all the method call.
  mojo::Remote<Interface> proxy_remote_;
  // Keeps the connection until the real remote is set.
  mojo::PendingReceiver<Interface> pending_receiver_;
  // Used to connect to the real remote.
  std::unique_ptr<mojo::Receiver<Interface>> proxy_receiver_;
  // The real remote object.
  mojo::Remote<Interface> real_remote_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_IMPL_H_
