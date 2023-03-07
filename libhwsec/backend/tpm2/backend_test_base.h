// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_BACKEND_TEST_BASE_H_
#define LIBHWSEC_BACKEND_TPM2_BACKEND_TEST_BASE_H_

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/middleware/middleware_owner.h"
#include "libhwsec/proxy/proxy_for_test.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2TestBase : public ::testing::Test {
 public:
  BackendTpm2TestBase() = default;
  BackendTpm2TestBase(const BackendTpm2TestBase&) = delete;
  BackendTpm2TestBase& operator=(const BackendTpm2TestBase&) = delete;
  ~BackendTpm2TestBase() override = default;

  void SetUp() override {
    proxy_ = std::make_unique<ProxyForTest>();

    auto backend =
        std::make_unique<BackendTpm2>(*proxy_, MiddlewareDerivative{});
    backend_ = backend.get();

    middleware_owner_ = std::make_unique<MiddlewareOwner>(
        std::move(backend), ThreadingMode::kCurrentThread);

    backend_->set_middleware_derivative_for_test(middleware_owner_->Derive());
  }

 protected:
  std::unique_ptr<ProxyForTest> proxy_;
  std::unique_ptr<MiddlewareOwner> middleware_owner_;
  BackendTpm2* backend_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_BACKEND_TEST_BASE_H_
