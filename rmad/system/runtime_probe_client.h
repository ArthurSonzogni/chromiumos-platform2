// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_
#define RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_

#include <string>
#include <utility>
#include <vector>

#include <rmad/proto_bindings/rmad.pb.h>

namespace rmad {

using ComponentsWithIdentifier =
    std::vector<std::pair<RmadComponent, std::string>>;

class RuntimeProbeClient {
 public:
  RuntimeProbeClient() = default;
  virtual ~RuntimeProbeClient() = default;

  // Probe the components specified in |categories|, and set it in |components|
  // if a component is probed. If |categories| is empty, the function probes all
  // categories by default. Return true if the probing is successful. Return
  // false if the probing fails, and in this case |components| is not modified.
  virtual bool ProbeCategories(const std::vector<RmadComponent>& categories,
                               ComponentsWithIdentifier* components) = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_
