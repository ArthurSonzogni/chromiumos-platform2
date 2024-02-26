// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_LIBS_BLUR_DETECTOR_H_
#define CAMERA_DIAGNOSTICS_LIBS_BLUR_DETECTOR_H_

#include <cstdint>
#include <memory>

#include <base/files/file.h>

namespace cros {

// Wrapper around the Blur Detector C bindings that are imported
// from the libblurdetector.so.
class BlurDetector {
 public:
  virtual ~BlurDetector() = default;

  // Factory function for creating BlurDetectors.
  // This loads dlc_root_path/libblurdetector.so only once and leaves it
  // loaded, destructor of BlurDetector will not trigger dlclose().
  // Check the returned pointer for `nullptr` to determine success.
  // **Important** Not thread-safe.
  static std::unique_ptr<BlurDetector> Create(
      const base::FilePath& dlc_root_path);

  virtual bool DirtyLensProbabilityFromNV12(const uint8_t* data,
                                            const uint32_t height,
                                            const uint32_t width,
                                            float* dirty_probability) = 0;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_LIBS_BLUR_DETECTOR_H_
