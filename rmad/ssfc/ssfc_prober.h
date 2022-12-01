// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SSFC_SSFC_PROBER_H_
#define RMAD_SSFC_SSFC_PROBER_H_

#include <cstdint>
#include <memory>

#include "rmad/system/runtime_probe_client.h"
#include "rmad/utils/cros_config_utils.h"

namespace rmad {

class SsfcProber {
 public:
  SsfcProber() = default;
  virtual ~SsfcProber() = default;

  virtual uint32_t ProbeSSFC() const = 0;
};

class SsfcProberImpl : public SsfcProber {
 public:
  SsfcProberImpl();
  // Used to inject mocked |RuntimeProbeClient| and |CrosConfigUtils| for
  // testing.
  explicit SsfcProberImpl(
      std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
      std::unique_ptr<CrosConfigUtils> cros_config_utils);
  ~SsfcProberImpl() override = default;

  uint32_t ProbeSSFC() const override;

 private:
  std::unique_ptr<RuntimeProbeClient> runtime_probe_client_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
};

}  // namespace rmad

#endif  // RMAD_SSFC_SSFC_PROBER_H_
