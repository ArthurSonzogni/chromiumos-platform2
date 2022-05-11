// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_

#include <memory>
#include <utility>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

namespace diagnostics {

// MojoRelay can bind a mojo::PendingRemote to a mojo::Remote, by using
// |mojo::FusePipes|. In cases we cannot create a remote directly (e.g.
// retrieving a remote from another interface), this can be used as a remote
// object so all the method calls can be queued until the interface is
// connected.
template <typename Interface>
class MojoRelay {
 public:
  MojoRelay() = default;
  ~MojoRelay() = default;
  MojoRelay(const MojoRelay&) = delete;
  MojoRelay& operator=(const MojoRelay&) = delete;

 public:
  // Initialize the relay so the |remote_| can be used.
  void InitNewPipeAndWaitForIncomingRemote() {
    pending_receiver_ = remote_.BindNewPipeAndPassReceiver();
  }

  // Returns the pointer of the Interface.
  auto Get() { return remote_.get(); }

  // Binds the real remote object to this relay.
  bool Bind(mojo::PendingRemote<Interface> pending_remote) {
    CHECK(pending_receiver_.is_valid());
    return mojo::FusePipes(std::move(pending_receiver_),
                           std::move(pending_remote));
  }

 private:
  // Used to queue all the method call.
  mojo::Remote<Interface> remote_;
  // Keeps the connection until the real remote is set.
  mojo::PendingReceiver<Interface> pending_receiver_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_
