// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_

#include <gmock/gmock.h>

#include <base/files/scoped_temp_dir.h>
#include <debugd/dbus-proxy-mocks.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/system/helper_invoker_direct_impl.h"

namespace runtime_probe {

class ContextMockImpl : public Context {
 public:
  ContextMockImpl();
  ~ContextMockImpl() override;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    return &mock_debugd_proxy_;
  };

  HelperInvoker* helper_invoker() override { return &helper_invoker_direct_; }

  const base::FilePath& root_dir() override { return root_dir_; }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return &mock_debugd_proxy_;
  }

 private:
  testing::StrictMock<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  HelperInvokerDirectImpl helper_invoker_direct_;

  // Used to create a temporary root directory.
  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
