// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/ssfc/ssfc_prober.h"

#include <memory>
#include <utility>

#include "rmad/system/runtime_probe_client_impl.h"
#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

SsfcProberImpl::SsfcProberImpl() {
  runtime_probe_client_ =
      std::make_unique<RuntimeProbeClientImpl>(GetSystemBus());
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
}

SsfcProberImpl::SsfcProberImpl(
    std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
    std::unique_ptr<CrosConfigUtils> cros_config_utils)
    : runtime_probe_client_(std::move(runtime_probe_client)),
      cros_config_utils_(std::move(cros_config_utils)) {}

uint32_t SsfcProberImpl::ProbeSSFC() const {
  return 0;
}

}  // namespace rmad
