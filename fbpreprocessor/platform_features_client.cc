// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/platform_features_client.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

namespace {
constexpr char kAllowFirmwareDumpsFeatureName[] =
    "CrOSLateBootAllowFirmwareDumps";

const VariationsFeature kAllowFirmwareDumpsFeature{
    kAllowFirmwareDumpsFeatureName, FEATURE_DISABLED_BY_DEFAULT};

constexpr char kAllowFirmwareDumpsFlagPath[] =
    "/run/fbpreprocessord/allow_firmware_dumps";
}  // namespace

namespace fbpreprocessor {

PlatformFeaturesClient::PlatformFeaturesClient() : allowed_(false) {}

void PlatformFeaturesClient::Start(dbus::Bus* bus) {
  LOG(INFO) << "Initializing.";
  CHECK(feature::PlatformFeatures::Initialize(bus))
      << "Failed to initialize PlatformFeatures library.";
  feature_lib_ = feature::PlatformFeatures::Get();
  feature_lib_->ListenForRefetchNeeded(
      base::BindRepeating(&PlatformFeaturesClient::Refetch,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&PlatformFeaturesClient::OnConnected,
                     weak_factory_.GetWeakPtr()));
}

void PlatformFeaturesClient::Refetch() {
  feature_lib_->IsEnabled(kAllowFirmwareDumpsFeature,
                          base::BindOnce(&PlatformFeaturesClient::OnFetched,
                                         weak_factory_.GetWeakPtr()));
}

void PlatformFeaturesClient::OnConnected(bool ready) {
  if (ready) {
    LOG(INFO) << "Ready to fetch PlatformFeatures.";
    Refetch();
  }
}

void PlatformFeaturesClient::OnFetched(bool allowed) {
  LOG(INFO) << "Firmware dumps allowed: " << allowed;
  bool notify_observers = allowed_ != allowed;
  allowed_ = allowed;
  if (notify_observers) {
    for (auto& observer : observers_) {
      observer.OnFeatureChanged(allowed_);
    }
  }
  // Write the value of the Finch flag to disk. Instead of having to query the
  // flag, the other processes involved in the feature (crash-report, debugd)
  // will simply read the content of the file. That makes implementation less
  // invasive in those platform-critical processes.
  if (!base::WriteFile(base::FilePath(kAllowFirmwareDumpsFlagPath),
                       allowed_ ? "1" : "0")) {
    LOG(ERROR) << "Failed to write feature flag to disk.";
  }
}

void PlatformFeaturesClient::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PlatformFeaturesClient::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace fbpreprocessor
