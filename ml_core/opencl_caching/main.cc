// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/syslog_logging.h>

#include "ml_core/dlc/dlc_loader.h"
#include "ml_core/effects_pipeline.h"

namespace {
const char kForceEnableEffectsPath[] = "/run/camera/force_enable_effects";
base::WaitableEvent kEffectApplied;
void SetEffectCallback(bool success) {
  kEffectApplied.Signal();
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // TODO(jmpollock): Once the correct API for feature selection is implemented,
  //                  replace this with that.
  if (!base::PathExists(base::FilePath(kForceEnableEffectsPath))) {
    LOG(WARNING) << "Effects feature not enabled, exiting.";
    return EX_OK;
  }

  cros::DlcLoader dlc_loader;
  dlc_loader.Run();
  if (!dlc_loader.DlcLoaded()) {
    LOG(ERROR) << "Couldn't install DLC. Exiting.";
    return EX_SOFTWARE;
  }

  auto pipeline =
      cros::EffectsPipeline::Create(dlc_loader.GetDlcRootPath(), nullptr);

  if (!pipeline) {
    LOG(ERROR) << "Couldn't create pipeline, Exiting.";
    return EX_SOFTWARE;
  }

  auto config = cros::EffectsConfig();
  config.segmentation_gpu_api = cros::GpuApi::kOpenCL;
  config.relighting_gpu_api = cros::GpuApi::kOpenCL;

  base::TimeTicks start = base::TimeTicks::Now();
  LOG(INFO) << "Loading graph to build OpenCL Cache";
  config.blur_enabled = true;
  config.relight_enabled = true;
  kEffectApplied.Reset();
  pipeline->SetEffect(&config, SetEffectCallback);
  kEffectApplied.Wait();
  LOG(INFO) << "Completed in " << (base::TimeTicks::Now() - start);

  return EX_OK;
}
