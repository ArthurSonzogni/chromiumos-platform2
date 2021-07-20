// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_
#define RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_

#include <memory>
#include <string>

#include <base/optional.h>

#include "runtime_probe/function_templates/network.h"

namespace runtime_probe {

class CellularNetworkFunction : public NetworkFunction {
  using NetworkFunction::NetworkFunction;

 public:
  NAME_PROBE_FUNCTION("cellular_network");

 protected:
  base::Optional<std::string> GetNetworkType() const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_CELLULAR_NETWORK_H_
