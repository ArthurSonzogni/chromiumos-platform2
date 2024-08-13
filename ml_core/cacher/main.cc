// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/syslog_logging.h>

#include "ml_core/cacher/constants.h"
#include "ml_core/cacher/utils.h"
#include "ml_core/dlc/dlc_ids.h"
#include "ml_core/dlc/dlc_loader.h"
#include "ml_core/effects_pipeline.h"
#include "ml_core/effects_pipeline_types.h"

namespace {
const char kForceEnableEffectsPath[] = "/run/camera/force_enable_effects";
// TODO(imranziad): Evaluate the risks of having a fixed temp directory.
const char kTempCacheDir[] = "/tmp/ml_core_cache";
#if USE_INTEL_OPENVINO_DELEGATE
constexpr char kStableDelegateSettingsFile[] =
    "/etc/ml_core/stable_delegate_settings.json";
#endif
base::WaitableEvent kEffectApplied;

void SetEffectCallback(bool success) {
  kEffectApplied.Signal();
}

}  // namespace

bool UpdateCache(const base::FilePath& effects_lib_path,
                 cros::EffectsConfig config,
                 const base::FilePath& target_dir) {
  LOG(INFO) << "Start cache update for " << target_dir;

  base::FilePath temp_cache_dir_path(kTempCacheDir);
  base::ScopedTempDir new_cache_dir;
  if (!new_cache_dir.Set(temp_cache_dir_path)) {
    LOG(ERROR) << "ERROR: Unable to create temporary directory.";
    return false;
  }
  base::FilePath cache_path(new_cache_dir.GetPath());

  // |caching_dir_override| is only applicable for OpenCL GPU delegate. It's
  // ignored for others.
  auto pipeline =
      cros::EffectsPipeline::Create(effects_lib_path, nullptr, cache_path);

  if (!pipeline) {
    LOG(ERROR) << "Couldn't create pipeline, Exiting.";
    return false;
  }

  base::TimeTicks start = base::TimeTicks::Now();
  LOG(INFO) << "Running effects graph to compile cache";
  kEffectApplied.Reset();
  pipeline->SetEffect(&config, SetEffectCallback);
  kEffectApplied.Wait();
  pipeline.reset();  // Force cache files to be flushed to disk
  LOG(INFO) << "Cache generated in " << (base::TimeTicks::Now() - start);

  LOG(INFO) << "Clearing cache dir and transferring new cache files";
  // Clear out any stale files in the cache
  cros::ClearCacheDirectory(target_dir);
  // Update the cache dir with newly generated files
  cros::CopyCacheFiles(cache_path, target_dir);
  LOG(INFO) << "Cache updated: " << target_dir;

  return true;
}

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // TODO(jmpollock): Once the correct API for feature selection is implemented,
  //                  replace this with that.
  if (!base::PathExists(base::FilePath(kForceEnableEffectsPath))) {
    LOG(WARNING) << "Effects feature not enabled, exiting.";
    return EX_OK;
  }

  cros::DlcLoader dlc_loader(cros::dlc_client::kMlCoreDlcId);
  dlc_loader.Run();
  if (!dlc_loader.DlcLoaded()) {
    LOG(ERROR) << "Couldn't install DLC. Exiting.";
    return EX_SOFTWARE;
  }

  bool update_failed = false;

  // Update OpenCL Cache.
  {
    LOG(INFO) << "Prepare OpenCL cache";
    auto config = cros::EffectsConfig();
    config.segmentation_delegate = cros::Delegate::kGpu;
    config.relighting_delegate = cros::Delegate::kGpu;
    config.segmentation_gpu_api = cros::GpuApi::kOpenCL;
    config.relighting_gpu_api = cros::GpuApi::kOpenCL;
    config.blur_enabled = true;
    config.relight_enabled = true;

    if (!UpdateCache(dlc_loader.GetDlcRootPath(), config,
                     base::FilePath(cros::kOpenCLCachingDir))) {
      LOG(ERROR) << "Failed to update OpenCL Cache";
      update_failed = true;
    }
  }

#if USE_INTEL_OPENVINO_DELEGATE
  // Update OpenVINO Cache.
  {
    LOG(INFO) << "Prepare OpenVINO cache";
    auto config = cros::EffectsConfig();
    config.segmentation_delegate = cros::Delegate::kStable;
    config.relighting_delegate = cros::Delegate::kStable;
    static_assert(sizeof(kStableDelegateSettingsFile) <=
                  sizeof(config.stable_delegate_settings_file));
    base::strlcpy(config.stable_delegate_settings_file,
                  kStableDelegateSettingsFile,
                  sizeof(config.stable_delegate_settings_file));
    config.blur_enabled = true;
    config.relight_enabled = true;

    if (!UpdateCache(dlc_loader.GetDlcRootPath(), config,
                     base::FilePath(cros::kOpenVinoCachingDir))) {
      LOG(ERROR) << "Failed to update OpenVINO Cache";
      update_failed = true;
    }
  }
#endif

  LOG(INFO) << "Cache update complete!";

  if (update_failed) {
    return EX_SOFTWARE;
  }

  return EX_OK;
}
