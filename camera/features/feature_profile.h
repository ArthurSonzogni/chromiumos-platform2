/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_FEATURE_PROFILE_H_
#define CAMERA_FEATURES_FEATURE_PROFILE_H_

#include <base/optional.h>
#include <base/values.h>

#include "common/reloadable_config_file.h"
#include "cros-camera/device_config.h"

namespace cros {

// FeatureProfile is a utility class that parses the device/model specific
// feature profile configs and exposes the feature settings.
class FeatureProfile {
 public:
  static constexpr char kFeatureProfileFilePath[] =
      "/etc/camera/feature_profile.json";

  enum class FeatureType {
    // CrOS face detection with key "face_detection".
    kFaceDetection,

    // CrOS Gcam AE with key "gcam_ae".
    kGcamAe,

    // CrOS HDRnet with key "hdrnet".
    kHdrnet,
  };

  // Creates a FeatureProfile instance with the given |feature_config| JSON data
  // and |device_config| hardware device configuration.
  //
  // If |feature_config| is nullopt, then by default  the config stored in
  // kFeatureProfileFilePath will be loaded. If |device_config| is nullopt, then
  // the default DeviceConfig instance from DeviceConfig::Create() will be used.
  FeatureProfile(base::Optional<base::Value> feature_config = base::nullopt,
                 base::Optional<DeviceConfig> device_config = base::nullopt);

  // Checks if |feature| is enabled.
  bool IsEnabled(FeatureType feature) const;

  // Gets the file path of the feature config file for |feature|. Returns an
  // empty path if there's not config path set for |feature|.
  base::FilePath GetConfigFilePath(FeatureType feature) const;

 private:
  void OnOptionsUpdated(const base::Value& json_values);

  struct FeatureSetting {
    // File path to the feature config file.
    base::FilePath config_file_path;
  };

  ReloadableConfigFile config_file_;
  base::Optional<DeviceConfig> device_config_;

  // The parsed feature settings.
  base::flat_map<FeatureType, FeatureSetting> feature_settings_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_FEATURE_PROFILE_H_
