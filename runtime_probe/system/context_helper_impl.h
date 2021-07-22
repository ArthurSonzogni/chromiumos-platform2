// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_

#include "runtime_probe/system/context.h"

namespace runtime_probe {

class ContextHelperImpl : public Context {
 public:
  ContextHelperImpl() = default;
  ~ContextHelperImpl() override = default;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    NOTREACHED() << "The helper should not call debugd.";
    return nullptr;
  };
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_HELPER_IMPL_H_
