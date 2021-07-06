// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_H_
#define RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_H_

#include <string>

namespace runtime_probe {

class RuntimeProbeHelperInvoker {
 public:
  virtual ~RuntimeProbeHelperInvoker() = default;

  // Invokes an individual helper instance to perform the actual probing actions
  // in a properly secured environment.  The |probe_statement| is the input
  // of the helper process.  The method is a blocking call that returns after
  // the helper process ends.  If it successes, the method stores the probed
  // result in |result| and returns |true|; otherwise, the method returns
  // |false|.
  virtual bool Invoke(const std::string& probe_statement,
                      std::string* result) = 0;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_HELPER_INVOKER_H_
