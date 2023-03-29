// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_
#define LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_

#include <string>

#include <brillo/brillo_export.h>
#include <libsegmentation/feature_management_interface.h>

namespace segmentation {

// An implementation that invokes the corresponding functions provided
// in feature_management_interface.h.
class BRILLO_EXPORT FeatureManagementImpl : public FeatureManagementInterface {
 public:
  bool IsFeatureEnabled(const std::string& name) const override;

  int GetFeatureLevel() const override;
};

}  // namespace segmentation

#endif  // LIBSEGMENTATION_FEATURE_MANAGEMENT_IMPL_H_
