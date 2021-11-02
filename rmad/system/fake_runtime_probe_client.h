// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_RUNTIME_PROBE_CLIENT_H_
#define RMAD_SYSTEM_FAKE_RUNTIME_PROBE_CLIENT_H_

#include "rmad/system/runtime_probe_client.h"

#include <set>
#include <vector>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace rmad {
namespace fake {

class FakeRuntimeProbeClient : public RuntimeProbeClient {
 public:
  FakeRuntimeProbeClient() = default;
  FakeRuntimeProbeClient(const FakeRuntimeProbeClient&) = delete;
  FakeRuntimeProbeClient& operator=(const FakeRuntimeProbeClient&) = delete;
  ~FakeRuntimeProbeClient() override = default;

  bool ProbeCategories(const std::vector<RmadComponent>& categories,
                       std::set<RmadComponent>* components) override;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_RUNTIME_PROBE_CLIENT_H_
