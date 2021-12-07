// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_LOCAL_STATE_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_LOCAL_STATE_H_

#include <memory>

#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "diagnostics/bindings/connectivity/mojom/state.mojom.h"

namespace diagnostics {
namespace bindings {
namespace connectivity {

// LocalState provides interface to set the local internal state of
// connectivity test between two context object in each processes.
class LocalState {
 public:
  LocalState(const LocalState&) = delete;
  LocalState& operator=(const LocalState&) = delete;
  virtual ~LocalState() = default;

  static std::unique_ptr<LocalState> Create(
      mojo::PendingReceiver<mojom::State> receiver);

 protected:
  LocalState() = default;
};

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_LOCAL_STATE_H_
