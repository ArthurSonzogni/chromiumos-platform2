// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_MOCK_FRONTEND_H_

#include <memory>
#include <utility>

#include "libhwsec/backend/mock_backend.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

class MockFrontend {
 public:
  MockFrontend() {}
  virtual ~MockFrontend() = default;

  MiddlewareDerivative GetFakeMiddlewareDerivative() {
    return MiddlewareDerivative{
        .task_runner = base::SequencedTaskRunnerHandle::IsSet()
                           ? base::SequencedTaskRunnerHandle::Get()
                           : nullptr,
        .thread_id = base::PlatformThread::CurrentId(),
        .middleware = nullptr,
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_MOCK_FRONTEND_H_
