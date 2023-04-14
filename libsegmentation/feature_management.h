// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_H_

#include <memory>
#include <string>
#include <utility>

#include <brillo/brillo_export.h>
#include <libsegmentation/feature_management_interface.h>

namespace segmentation {

// C++ class to access feature_management system properties.
class BRILLO_EXPORT FeatureManagement {
 public:
  // Default implementation uses the real feature_management
  // (FeatureManagementImpl).
  FeatureManagement();

  // Can be used to instantiate a fake implementation for testing by passing
  // FeatureManagementFake.
  explicit FeatureManagement(std::unique_ptr<FeatureManagementInterface> impl);

  // Return true when a named feature can be used on the device.
  bool IsFeatureEnabled(const std::string& name) const;

  // Return the maximal feature level available on the device.
  int GetFeatureLevel() const;

 private:
  std::unique_ptr<FeatureManagementInterface> impl_;
};

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_H_
