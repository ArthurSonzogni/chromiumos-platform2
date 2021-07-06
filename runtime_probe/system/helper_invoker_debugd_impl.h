// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DEBUGD_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DEBUGD_IMPL_H_

#include "runtime_probe/system/helper_invoker.h"

#include <string>

namespace runtime_probe {

class RuntimeProbeHelperInvokerDebugdImpl : public RuntimeProbeHelperInvoker {
 public:
  // Invoke the helper replica via `debugd`'s D-Bus RPC.
  bool Invoke(const std::string& probe_statement, std::string* result) override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DEBUGD_IMPL_H_
