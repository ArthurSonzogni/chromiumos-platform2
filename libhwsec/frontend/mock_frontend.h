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
  MockFrontend() {
    auto mock_backend = std::make_unique<MockBackend>();
    mock_backend_ = mock_backend.get();
    middleware_owner_ = std::make_unique<MiddlewareOwner>(
        std::move(mock_backend),
        base::SequencedTaskRunnerHandle::IsSet()
            ? base::SequencedTaskRunnerHandle::Get()
            : nullptr,
        base::PlatformThread::CurrentId());
  }
  virtual ~MockFrontend() = default;

  MockBackend* GetMockBackend() { return mock_backend_; }
  MiddlewareOwner* GetMiddlewareOwner() { return middleware_owner_.get(); }

 private:
  MockBackend* mock_backend_;
  std::unique_ptr<MiddlewareOwner> middleware_owner_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_MOCK_FRONTEND_H_
