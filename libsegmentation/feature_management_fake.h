// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_FAKE_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_FAKE_H_

#include <set>
#include <string>

#include <brillo/brillo_export.h>
#include <libsegmentation/feature_management.h>

namespace segmentation {

namespace fake {

// A fake implementation that simulates the manipulation of features properties
// with an in-memory table.  By default, all features are unset so getters
// will return |false| or 0.
class BRILLO_EXPORT FeatureManagementFake : public FeatureManagementInterface {
 public:
  FeatureManagementFake() = default;

  bool IsFeatureEnabled(const std::string& name) const override;

  FeatureLevel GetFeatureLevel() override;

  // Set the feature level for the device.
  //
  // @param level: the feature level.
  void SetFeatureLevel(FeatureLevel level);

  // Set the value of the specific feature.
  //
  // After the feature is set, IsFeatureEnabled() returns |true|.
  //
  // @param name The name of feature.
  void SetFeature(const std::string& name);

  // Unset the value of the specific feature.
  //
  // After the feature is unset, IsFeatureEnabled() returns |false|.
  //
  // @param name The name of feature.
  void UnsetFeature(const std::string& name);

 private:
  std::set<std::string> system_features_properties_;
  FeatureLevel system_features_level_;
};

}  // namespace fake

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_FAKE_H_
