/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_CLIENT_H_
#define CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_CLIENT_H_

#include <map>
#include <memory>

#include <base/callback.h>
#include <base/optional.h>
#include <base/synchronization/lock.h>

#include "common/camera_buffer_pool.h"
#include "cros-camera/auto_framing_cros.h"
#include "cros-camera/common_types.h"

namespace cros {

// This class interfaces with the Google3 auto-framing library:
// http://google3/chromeos/camera/lib/auto_framing/auto_framing_cros.h
class AutoFramingClient : public AutoFramingCrOS::Client {
 public:
  struct Options {
    Size input_size;
    double frame_rate = 0.0;
    uint32_t target_aspect_ratio_x = 0;
    uint32_t target_aspect_ratio_y = 0;
  };

  // Set up the pipeline.
  bool SetUp(const Options& options);

  // Process one frame.  |buffer| is only used during this function call.
  bool ProcessFrame(int64_t timestamp, buffer_handle_t buffer);

  // Return the stored ROI if a new detection is available, or nullopt if not.
  // After this call the stored ROI is cleared, waiting for another new
  // detection to fill it.
  base::Optional<Rect<uint32_t>> TakeNewRegionOfInterest();

  // Gets the crop window calculated by the full auto-framing pipeline.
  Rect<uint32_t> GetCropWindow();

  // Tear down the pipeline and clear states.
  void TearDown();

  // Implementations of AutoFramingCrOS::Client.
  void OnNewRegionOfInterest(
      int64_t timestamp, int x_min, int y_min, int x_max, int y_max) override;
  void OnNewCropWindow(
      int64_t timestamp, int x_min, int y_min, int x_max, int y_max) override;
  void OnNewAnnotatedFrame(int64_t timestamp,
                           const uint8_t* data,
                           int stride) override;

 private:
  std::unique_ptr<AutoFramingCrOS> auto_framing_;
  std::unique_ptr<CameraBufferPool> buffer_pool_;
  std::map<int64_t, CameraBufferPool::Buffer> inflight_buffers_;

  base::Lock lock_;
  base::Optional<Rect<uint32_t>> region_of_interest_ GUARDED_BY(lock_);
  Rect<uint32_t> crop_window_ GUARDED_BY(lock_);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_CLIENT_H_
