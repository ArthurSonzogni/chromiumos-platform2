// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <brillo/errors/error.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace diagnostics {
namespace {

class MockCallback {
 public:
  MOCK_METHOD(void,
              OnStringResultCallback,
              (const std::string&, brillo::Error*));
};

class DebugdAdapterImplTest : public ::testing::Test {
 public:
  DebugdAdapterImplTest()
      : debugd_proxy_mock_(new StrictMock<org::chromium::debugdProxyMock>()),
        debugd_adapter_(std::make_unique<DebugdAdapterImpl>(
            std::unique_ptr<org::chromium::debugdProxyMock>(
                debugd_proxy_mock_))) {}
  DebugdAdapterImplTest(const DebugdAdapterImplTest&) = delete;
  DebugdAdapterImplTest& operator=(const DebugdAdapterImplTest&) = delete;

 protected:
  StrictMock<MockCallback> callback_;

  // Owned by |debugd_adapter_|.
  StrictMock<org::chromium::debugdProxyMock>* debugd_proxy_mock_;

  std::unique_ptr<DebugdAdapter> debugd_adapter_;
};

}  // namespace
}  // namespace diagnostics
