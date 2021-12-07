// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_CONTEXT_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_CONTEXT_H_

#include <memory>
#include <string>

namespace diagnostics {
namespace bindings {
namespace connectivity {

class LocalState;
class RemoteState;

// Context contains the objects used by the connectivity test objects.
class Context {
 public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context() = default;

  static std::unique_ptr<Context> Create(
      std::unique_ptr<LocalState> local_state,
      std::unique_ptr<RemoteState> remote_state);

 public:
  // The local state interface.
  virtual LocalState* local_state() = 0;
  // The remote state interface.
  virtual RemoteState* remote_state() = 0;

 protected:
  Context() = default;
};

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_CONTEXT_H_
