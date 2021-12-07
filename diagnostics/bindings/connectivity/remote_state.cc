// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/bindings/connectivity/remote_state.h"

#include <utility>

#include <mojo/public/cpp/bindings/remote.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {

class RemoteStateImpl : public RemoteState {
 public:
  explicit RemoteStateImpl(mojo::PendingRemote<mojom::State> remote)
      : remote_(std::move(remote)) {}
  RemoteStateImpl(const RemoteStateImpl&) = delete;
  RemoteStateImpl& operator=(const RemoteStateImpl&) = delete;
  virtual ~RemoteStateImpl() = default;

 private:
  mojo::Remote<mojom::State> remote_;
};

std::unique_ptr<RemoteState> RemoteState::Create(
    mojo::PendingRemote<mojom::State> remote) {
  return std::make_unique<RemoteStateImpl>(std::move(remote));
}

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics
