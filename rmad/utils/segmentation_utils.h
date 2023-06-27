// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_SEGMENTATION_UTILS_H_
#define RMAD_UTILS_SEGMENTATION_UTILS_H_

namespace rmad {

class SegmentationUtils {
 public:
  SegmentationUtils() = default;
  virtual ~SegmentationUtils() = default;

  // Returns true if feature is available on the model.
  virtual bool IsFeatureEnabled() const = 0;

  // Returns true if feature values are provisioned on the device.
  virtual bool IsFeatureProvisioned() const = 0;

  // Returns the feature level according to the provisioned values.
  virtual int GetFeatureLevel() const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SEGMENTATION_UTILS_H_
