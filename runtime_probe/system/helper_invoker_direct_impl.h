// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DIRECT_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DIRECT_IMPL_H_

#include "runtime_probe/system/helper_invoker.h"

#include <string>

namespace runtime_probe {

class HelperInvokerDirectImpl : public HelperInvoker {
  using HelperInvoker::HelperInvoker;

 public:
  // Invoke the helper replica by running the subprocess directly.
  //
  // The implementation is only available when |USE_FACTORY_RUNTIME_PROBE| is
  // set.  `factory_runtime_probe` is specialized for the factory environment.
  // It is designed to be able to run without helps from the rootfs.  Hence
  // `debugd` can't help in this scenario.  Combining with the fact that
  // security is not a critical factor in the factory environment, calling
  // subprocesses directly becomes a valid alternative.
  bool Invoke(const ProbeFunction* probe_function,
              const std::string& probe_statement_str,
              std::string* result) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_DIRECT_IMPL_H_
