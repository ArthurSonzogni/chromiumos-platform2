// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/bindings/connectivity/local_state.h"

#include <utility>

#include <mojo/public/cpp/bindings/receiver.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {

class LocalStateImpl : public LocalState, public mojom::State {
 public:
  explicit LocalStateImpl(mojo::PendingReceiver<mojom::State> receiver)
      : receiver_(this, std::move(receiver)) {}
  LocalStateImpl(const LocalStateImpl&) = delete;
  LocalStateImpl& operator=(const LocalStateImpl&) = delete;
  virtual ~LocalStateImpl() = default;

 private:
  mojo::Receiver<mojom::State> receiver_;
};

std::unique_ptr<LocalState> LocalState::Create(
    mojo::PendingReceiver<mojom::State> receiver) {
  return std::make_unique<LocalStateImpl>(std::move(receiver));
}

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics
