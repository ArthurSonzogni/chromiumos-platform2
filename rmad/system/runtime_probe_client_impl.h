// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_
#define RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_

#include <memory>
#include <vector>

#include <runtime_probe-client/runtime_probe/dbus-proxies.h>

#include "rmad/system/runtime_probe_client.h"

namespace rmad {

class RuntimeProbeClientImpl : public RuntimeProbeClient {
 public:
  RuntimeProbeClientImpl();
  explicit RuntimeProbeClientImpl(
      std::unique_ptr<org::chromium::RuntimeProbeProxyInterface>
          runtime_probe_proxy);
  RuntimeProbeClientImpl(const RuntimeProbeClientImpl&) = delete;
  RuntimeProbeClientImpl& operator=(const RuntimeProbeClientImpl&) = delete;

  ~RuntimeProbeClientImpl() override;

  bool ProbeCategories(const std::vector<RmadComponent>& categories,
                       bool use_customized_identifier,
                       ComponentsWithIdentifier* components) override;
  bool ProbeSsfcComponents(bool use_customized_identifier,
                           ComponentsWithIdentifier* components) override;

 private:
  // The proxy object for runtime_probe dbus service.
  std::unique_ptr<org::chromium::RuntimeProbeProxyInterface>
      runtime_probe_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_RUNTIME_PROBE_CLIENT_IMPL_H_
