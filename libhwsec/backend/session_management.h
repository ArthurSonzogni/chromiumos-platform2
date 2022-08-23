// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_SESSION_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_SESSION_MANAGEMENT_H_

#include <cstdint>

#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/session.h"

namespace hwsec {

class BackendTpm2;

// SessionManagement provide the functions to manager session.
class SessionManagement {
 public:
  struct CreateSessionOptions {};

  // Creates a session with |policy| and optional |options|.
  virtual StatusOr<ScopedSession> CreateSession(
      const OperationPolicy& policy, CreateSessionOptions options) = 0;

  // Flushes the |session| to reclaim the resource.
  virtual Status Flush(Session session) = 0;

  // Loads the session with raw |session_handle|.
  // TODO(174816474): deprecated legacy APIs.
  virtual StatusOr<ScopedSession> SideLoadSession(uint32_t session_handle) = 0;

 protected:
  SessionManagement() = default;
  ~SessionManagement() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_SESSION_MANAGEMENT_H_
