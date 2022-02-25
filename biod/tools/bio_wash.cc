// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a utility to clear internal crypto entropy (if applicable) from
// |BiometricsManager|s, so as to render useless templates and other user data
// encrypted with old secrets.

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/task/single_thread_task_executor.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <cros_config/cros_config.h>
#include <dbus/bus.h>

#include "biod/biod_config.h"
#include "biod/biod_storage.h"
#include "biod/biod_version.h"
#include "biod/cros_fp_biometrics_manager.h"
#include "biod/cros_fp_device.h"
#include "biod/power_button_filter.h"

namespace {

static constexpr base::TimeDelta kTimeout = base::Seconds(30);

constexpr char kHelpMessage[] = "bio_wash resets the SBP.";

bool IsFingerprintUnsupported() {
  brillo::CrosConfig cros_config;
  if (!cros_config.Init()) {
    LOG(WARNING) << "Cros config is not supported on this model, continuing "
                    "in legacy mode.";
    return false;
  }
  if (biod::FingerprintUnsupported(&cros_config)) {
    return true;
  }
  return false;
}

int DoBioWash(const bool factory_init = false) {
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());
  std::vector<std::unique_ptr<biod::BiometricsManager>> managers;
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  // It's o.k to not connect to the bus as we don't really care about D-Bus
  // events for BioWash.
  auto bus = base::MakeRefCounted<dbus::Bus>(options);
  auto biod_metrics = std::make_unique<biod::BiodMetrics>();
  auto biod_storage =
      std::make_unique<biod::BiodStorage>(biod::kCrosFpBiometricsManagerName);
  // Add all the possible BiometricsManagers available.
  auto cros_fp_bio = std::make_unique<biod::CrosFpBiometricsManager>(
      biod::PowerButtonFilter::Create(bus),
      biod::CrosFpDevice::Create(biod_metrics.get(),
                                 std::make_unique<ec::EcCommandFactory>()),
      std::move(biod_metrics),
      std::make_unique<biod::CrosFpRecordManager>(std::move(biod_storage)));
  if (cros_fp_bio) {
    managers.emplace_back(std::move(cros_fp_bio));
  }

  if (managers.empty()) {
    LOG(ERROR) << "No biometrics managers instantiated correctly.";
    return -1;
  }

  int ret = 0;
  for (const auto& biometrics_manager : managers) {
    if (!biometrics_manager->ResetEntropy(factory_init)) {
      LOG(ERROR) << "Failed to reset entropy for sensor type: "
                 << biometrics_manager->GetType();
      ret = -1;
    }
  }

  return ret;
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(factory_init, false, "First time initialisation in the factory.");
  DEFINE_bool(force, false, "Override cros config fingerprint system check.");

  brillo::FlagHelper::Init(argc, argv, kHelpMessage);

  biod::LogVersion();

  // Check if model supports fingerprint
  if (!FLAGS_force && IsFingerprintUnsupported()) {
    LOG(INFO) << "Fingerprint is not supported on this model, exiting.";
    return EXIT_SUCCESS;
  }

  pid_t pid;
  pid = fork();

  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork child process for bio_wash.";
    return -1;
  }

  if (pid == 0) {
    return DoBioWash(FLAGS_factory_init);
  }

  auto process = base::Process::Open(pid);
  int exit_code;
  if (!process.WaitForExitWithTimeout(kTimeout, &exit_code)) {
    LOG(ERROR) << "Bio wash timeout out, exit code: " << exit_code;
    process.Terminate(-1, false);
  }

  return exit_code;
}
