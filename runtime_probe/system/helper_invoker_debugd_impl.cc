// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/helper_invoker_debugd_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/errors/error.h>
#include <debugd/dbus-proxies.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/pipe_utils.h"

namespace runtime_probe {

bool HelperInvokerDebugdImpl::Invoke(const ProbeFunction* probe_function,
                                     const std::string& probe_statement_str,
                                     std::string* result) const {
  base::ScopedFD read_fd{};
  brillo::ErrorPtr error;
  if (!Context::Get()->debugd_proxy()->EvaluateProbeFunction(
          probe_statement_str, &read_fd, &error)) {
    LOG(ERROR) << "Debugd::EvaluateProbeFunction failed: "
               << error->GetMessage();
    return false;
  }

  if (!ReadNonblockingPipeToString(read_fd.get(), result)) {
    LOG(ERROR) << "Cannot read result from helper";
    return false;
  }
  return true;
}

}  // namespace runtime_probe
