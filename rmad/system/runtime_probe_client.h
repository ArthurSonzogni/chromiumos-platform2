// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_
#define RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_

#include <set>

#include <rmad/proto_bindings/rmad.pb.h>

namespace rmad {

class RuntimeProbeClient {
 public:
  RuntimeProbeClient() = default;
  virtual ~RuntimeProbeClient() = default;

  virtual bool ProbeCategories(std::set<RmadComponent>* components) = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_H_
