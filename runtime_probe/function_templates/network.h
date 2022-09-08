// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
#define RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_

#include <optional>
#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class NetworkFunction : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 protected:
  virtual std::optional<std::string> GetNetworkType() const = 0;

 private:
  DataType EvalImpl() const final;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
