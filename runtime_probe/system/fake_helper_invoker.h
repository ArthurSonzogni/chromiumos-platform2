// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_FAKE_HELPER_INVOKER_H_
#define RUNTIME_PROBE_SYSTEM_FAKE_HELPER_INVOKER_H_

#include "runtime_probe/system/helper_invoker.h"

#include <string>

namespace runtime_probe {

class FakeHelperInvoker : public HelperInvoker {
  using HelperInvoker::HelperInvoker;

 public:
  // Invoke the helper directly through |EvalInHelper| for unittest.
  bool Invoke(const ProbeFunction* probe_function,
              const std::string& probe_statement_str,
              std::string* result) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_FAKE_HELPER_INVOKER_H_
