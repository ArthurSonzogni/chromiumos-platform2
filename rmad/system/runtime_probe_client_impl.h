// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_
#define RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_

#include "rmad/system/runtime_probe_client.h"

#include <set>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <rmad/proto_bindings/rmad.pb.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace rmad {

class RuntimeProbeClientImpl : public RuntimeProbeClient {
 public:
  explicit RuntimeProbeClientImpl(const scoped_refptr<dbus::Bus>& bus);
  RuntimeProbeClientImpl(const RuntimeProbeClientImpl&) = delete;
  RuntimeProbeClientImpl& operator=(const RuntimeProbeClientImpl&) = delete;

  ~RuntimeProbeClientImpl() override = default;

  bool ProbeCategories(const std::vector<RmadComponent>& categories,
                       std::set<RmadComponent>* components) override;

 private:
  // Owned by external D-Bus bus.
  dbus::ObjectProxy* proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_
