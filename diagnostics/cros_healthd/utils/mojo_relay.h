// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

namespace diagnostics {
// MojoRelay can adapt a mojo::Remote to another mojo::Remote. In cases we
// cannot create a remote directly (e.g. retrieving a remote from another
// interface), this can be used as a remote object so all the method calls can
// be queued until the interface is connected.
// This is done by creating a proxy mojo remote/recevier pair. The proxy mojo
// recevier will be bound to the real mojo remote after available. So the
// behavior is the same as a bound mojo remote object.
template <typename Interface>
class MojoRelay {
 public:
  MojoRelay() = default;
  virtual ~MojoRelay() = default;
  MojoRelay(MojoRelay&) = delete;
  MojoRelay& operator=(MojoRelay&) = delete;

 public:
  // Returns the pointer of the Interface.
  virtual typename Interface::Proxy_* Get() = 0;
  // Returns true if the real remote is bound.
  virtual bool IsBound() = 0;
  // Binds the real remote object to this relay.
  virtual void Bind(mojo::PendingRemote<Interface> pending_remote) = 0;
};
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_RELAY_H_
