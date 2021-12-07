// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_

#include <memory>

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/bindings/connectivity/mojom/state.mojom.h"

namespace diagnostics {
namespace bindings {
namespace connectivity {

// RemoteState provides interface to get the remote internal state of
// connectivity test between two context object in each processes.
class RemoteState {
 public:
  RemoteState(const RemoteState&) = delete;
  RemoteState& operator=(const RemoteState&) = delete;
  virtual ~RemoteState() = default;

  static std::unique_ptr<RemoteState> Create(
      mojo::PendingRemote<mojom::State> remote);

 protected:
  RemoteState() = default;
};

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_
