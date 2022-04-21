// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_
#define LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy_for_test.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm1TestBase : public ::testing::Test {
 public:
  BackendTpm1TestBase() {}
  BackendTpm1TestBase(const BackendTpm1TestBase&) = delete;
  BackendTpm1TestBase& operator=(const BackendTpm1TestBase&) = delete;
  virtual ~BackendTpm1TestBase() {}

  void SetUp() override {
    proxy_ = std::make_unique<ProxyForTest>();

    auto backend =
        std::make_unique<BackendTpm1>(*proxy_, MiddlewareDerivative{});
    backend_ = backend.get();

    middleware_owner_ = std::make_unique<MiddlewareOwner>(
        std::move(backend),
        base::SequencedTaskRunnerHandle::IsSet()
            ? base::SequencedTaskRunnerHandle::Get()
            : nullptr,
        base::PlatformThread::CurrentId());

    backend_->set_middleware_derivative_for_test(middleware_owner_->Derive());

    middleware_ = std::make_unique<Middleware>(middleware_owner_->Derive());

    using testing::_;
    using testing::DoAll;
    using testing::Return;
    using testing::SetArgPointee;

    EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
        .WillRepeatedly(
            DoAll(SetArgPointee<0>(kDefaultContext), Return(TPM_SUCCESS)));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Context_Connect(kDefaultContext, nullptr))
        .WillRepeatedly(Return(TPM_SUCCESS));

    EXPECT_CALL(proxy_->GetMock().overalls,
                Ospi_Context_GetTpmObject(kDefaultContext, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<1>(kDefaultTpm), Return(TPM_SUCCESS)));
  }

 protected:
  static inline constexpr TSS_HCONTEXT kDefaultContext = 9876;
  static inline constexpr TSS_HTPM kDefaultTpm = 6543;

  std::unique_ptr<MiddlewareOwner> middleware_owner_;
  std::unique_ptr<Middleware> middleware_;
  std::unique_ptr<ProxyForTest> proxy_;
  BackendTpm1* backend_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_TEST_BASE_H_
