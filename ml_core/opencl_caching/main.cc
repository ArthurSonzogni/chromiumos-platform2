// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/syslog_logging.h>

#include "ml_core/dlc/dlc_loader.h"
#include "ml_core/effects_pipeline.h"

namespace {

base::WaitableEvent kEffectApplied;
void SetEffectCallback(bool success) {
  kEffectApplied.Signal();
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  cros::DlcLoader dlc_loader;
  dlc_loader.Run();
  if (!dlc_loader.DlcLoaded()) {
    LOG(ERROR) << "Couldn't install DLC. Exiting.";
    return EX_SOFTWARE;
  }

  auto pipeline = cros::EffectsPipeline::Create(dlc_loader.GetDlcRootPath());
  auto config = cros::EffectsConfig();
  config.segmentation_gpu_api = cros::mojom::GpuApi::kOpenCL;
  config.relighting_gpu_api = cros::mojom::GpuApi::kOpenCL;

  // TODO(jmpollock): Extend this to the full set of effects once
  //                  they've been set up in Google3 for caching.
  auto effects = {cros::mojom::CameraEffect::kNone,
                  cros::mojom::CameraEffect::kBackgroundBlur,
                  cros::mojom::CameraEffect::kPortraitRelight};

  for (auto effect : effects) {
    LOG(INFO) << "Setting effect: " << effect;
    config.effect = effect;
    kEffectApplied.Reset();
    pipeline->SetEffect(&config, SetEffectCallback);
    kEffectApplied.Wait();
  }
  LOG(INFO) << "Done.";
}
