// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/fake_helper_invoker.h"

#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

bool FakeHelperInvoker::Invoke(const ProbeFunction* probe_function,
                               const std::string& probe_statement_str,
                               std::string* result) const {
  int res = probe_function->EvalInHelper(result);
  return res == 0;
}

}  // namespace runtime_probe
