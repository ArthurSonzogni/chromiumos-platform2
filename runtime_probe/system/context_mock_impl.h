// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_

#include <gmock/gmock.h>

#include <debugd/dbus-proxy-mocks.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/system/helper_invoker_mock_impl.h"

namespace runtime_probe {

class ContextMockImpl : public Context {
 public:
  ContextMockImpl() = default;
  ~ContextMockImpl() override = default;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    return &mock_debugd_proxy_;
  };

  HelperInvoker* helper_invoker() override { return &fake_helper_invoker_; }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return &mock_debugd_proxy_;
  }

 private:
  testing::StrictMock<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  FakeHelperInvoker fake_helper_invoker_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
