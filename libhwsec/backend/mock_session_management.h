// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_SESSION_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_MOCK_SESSION_MANAGEMENT_H_

#include <cstdint>
#include <gmock/gmock.h>

#include "libhwsec/backend/session_management.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/session.h"

namespace hwsec {

class BackendTpm2;

class MockSessionManagement : public SessionManagement {
 public:
  MOCK_METHOD(StatusOr<ScopedSession>,
              CreateSession,
              (const OperationPolicy& policy, CreateSessionOptions options),
              (override));
  MOCK_METHOD(Status, Flush, (Session session), (override));
  MOCK_METHOD(StatusOr<ScopedSession>,
              SideLoadSession,
              (uint32_t session_handle),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_SESSION_MANAGEMENT_H_
