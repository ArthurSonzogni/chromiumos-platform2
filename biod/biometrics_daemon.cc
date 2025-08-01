// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_daemon.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <cros_config/cros_config.h>
#include <libhwsec/factory/factory_impl.h>

#include "biod/biod_config.h"
#include "biod/biometrics_manager_wrapper.h"
#include "biod/cros_fp_biometrics_manager.h"
#include "biod/power_button_filter.h"
#include "biod/utils.h"

namespace biod {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::ExportedObjectManager;
using dbus::ObjectPath;

namespace {

constexpr char kBiodDaemonStorePath[] = "/run/daemon-store/biod";
constexpr char kBioFwUpdaterLibPath[] = "/var/lib/bio_fw_updater";
constexpr char kFirmwareDir[] = "/opt/google/biod/fw";

}  // namespace

BiometricsDaemon::BiometricsDaemon() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";

  object_manager_ = std::make_unique<ExportedObjectManager>(
      bus_, ObjectPath(kBiodServicePath));

  auto sequencer = base::MakeRefCounted<AsyncEventSequencer>();
  object_manager_->RegisterAsync(
      sequencer->GetHandler("Manager.RegisterAsync() failed.", true));

  brillo::CrosConfig cros_config;
  std::optional<std::string> board_name = biod::FingerprintBoard(&cros_config);
  CHECK(board_name.has_value() && !board_name->empty())
      << "Fingerprint board name unavailable.";

  biod_metrics_ = std::make_unique<BiodMetrics>();
  auto cros_fp_device = CrosFpDevice::Create(
      biod_metrics_.get(), std::make_unique<ec::EcCommandFactory>());
  CHECK(cros_fp_device) << "Failed to initialize CrosFpDevice.";
  auto power_button_filter = PowerButtonFilter::Create(bus_);
  CHECK(power_button_filter) << "Failed to initialize PowerButtonFilter.";

  session_state_manager_ =
      std::make_unique<SessionStateManager>(bus_.get(), biod_metrics_.get());

  auto selector = std::make_unique<updater::FirmwareSelector>(
      base::FilePath(kBioFwUpdaterLibPath), base::FilePath(kFirmwareDir));
  CHECK(feature::PlatformFeatures::Initialize(bus_));
  biod_feature_ = std::make_unique<BiodFeature>(
      bus_, session_state_manager_.get(), feature::PlatformFeatures::Get(),
      std::move(selector));

  ObjectPath cros_fp_bio_path = ObjectPath(base::StringPrintf(
      "%s/%s", kBiodServicePath, kCrosFpBiometricsManagerName));
  // Sets the root path to /run/daemon-store/biod/, which is bound to
  // /home/root/<user_id>/biod/.
  auto biod_storage = std::make_unique<BiodStorage>(
      base::FilePath(kBiodDaemonStorePath), biod::kCrosFpBiometricsManagerName);

  auto cros_fp_bio = std::make_unique<CrosFpBiometricsManager>(
      std::move(power_button_filter), std::move(cros_fp_device),
      biod_metrics_.get(),
      std::make_unique<CrosFpRecordManager>(std::move(biod_storage)));
  if (cros_fp_bio) {
    biometrics_managers_.emplace_back(
        std::make_unique<BiometricsManagerWrapper>(
            std::move(cros_fp_bio), object_manager_.get(),
            session_state_manager_.get(), cros_fp_bio_path,
            sequencer->GetHandler(
                "Failed to register CrosFpBiometricsManager object", true),
            biod_metrics_.get()));
  } else {
    LOG(INFO) << "No CrosFpBiometricsManager detected.";
  }

  CHECK(bus_->RequestOwnershipAndBlock(kBiodServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));

  // Refresh primary user. If primary user is available then session state
  // manager will call OnUserLoggedIn method from BiometricsManagerWrapper.
  session_state_manager_->RefreshPrimaryUser();
}

}  // namespace biod
