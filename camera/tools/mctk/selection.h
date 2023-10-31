/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This class encapsulates an 8-tuple of V4L2 SELECTION rectangles.
 * Each of the rectangles can be missing - indicated by std::optional.
 *
 * The intention is to group the inputs/outputs of VIDIOC_G_SELECTION and
 * VIDIOC_SUBDEV_G_SELECTION, for each selection target.
 *
 * https://www.kernel.org/doc/html/v6.1/userspace-api/media/v4l/vidioc-g-selection.html
 * https://www.kernel.org/doc/html/v6.1/userspace-api/media/v4l/vidioc-subdev-g-selection.html
 * https://www.kernel.org/doc/html/v6.1/userspace-api/media/v4l/v4l2-selection-targets.html
 */

#ifndef CAMERA_TOOLS_MCTK_SELECTION_H_
#define CAMERA_TOOLS_MCTK_SELECTION_H_

#include <linux/videodev2.h>

#include <optional>

class V4lMcSelection {
 public:
  /* Public functions */

  /* True if any selection target is not std::nullopt */
  bool HasAny();

  /* Public variables */

  std::optional<struct v4l2_rect> crop_;
  std::optional<struct v4l2_rect> crop_default_;
  std::optional<struct v4l2_rect> crop_bounds_;
  std::optional<struct v4l2_rect> native_size_;

  std::optional<struct v4l2_rect> compose_;
  std::optional<struct v4l2_rect> compose_default_;
  std::optional<struct v4l2_rect> compose_bounds_;
  std::optional<struct v4l2_rect> compose_padded_;
};

#endif /* CAMERA_TOOLS_MCTK_SELECTION_H_ */
