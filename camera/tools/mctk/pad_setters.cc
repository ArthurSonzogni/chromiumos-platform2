/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Setters for abstract models of V4L2 pads.
 *
 * If the model has an fd for a kernel device set, then the setters will
 * propagate the new values to the kernel.
 *
 * Return values:
 *  - on success: true
 *  - on failure: false
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <optional>

#include "tools/mctk/debug.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"

/* This is a macro instead of a function so we can easily print
 * the ioctl name in the error message.
 */
#define VIDIOC_SUBDEV_S_WRAP(ioctl_num, ptr)                       \
  do {                                                             \
    if (this->fd_ent_) {                                           \
      if (ioctl(this->fd_ent_.value(), (ioctl_num), &(ptr)) < 0) { \
        MCTK_PERROR("ioctl(" #ioctl_num ")");                      \
        return false;                                              \
      }                                                            \
    }                                                              \
                                                                   \
    return true;                                                   \
  } while (0);

bool V4lMcPad::SetCrop(struct v4l2_rect& crop) {
  subdev_.crop = crop;

  struct v4l2_subdev_crop subdev_crop = {};
  subdev_crop.pad = desc_.index;
  subdev_crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  subdev_crop.rect = crop;

  VIDIOC_SUBDEV_S_WRAP(VIDIOC_SUBDEV_S_CROP, subdev_crop);
}

bool V4lMcPad::SetFmt(struct v4l2_mbus_framefmt& fmt) {
  subdev_.fmt = fmt;

  struct v4l2_subdev_format subdev_format = {};
  subdev_format.pad = desc_.index;
  subdev_format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  subdev_format.format = fmt;

  VIDIOC_SUBDEV_S_WRAP(VIDIOC_SUBDEV_S_FMT, subdev_format);
}

bool V4lMcPad::SetFrameInterval(struct v4l2_fract& frame_interval) {
  subdev_.frame_interval = frame_interval;

  struct v4l2_subdev_frame_interval subdev_frame_interval = {};
  subdev_frame_interval.pad = desc_.index;
  subdev_frame_interval.interval = frame_interval;

  VIDIOC_SUBDEV_S_WRAP(VIDIOC_SUBDEV_S_FRAME_INTERVAL, subdev_frame_interval);
}

bool V4lMcPad::SetSelection(__u32 target, struct v4l2_rect& r) {
  switch (target) {
    case V4L2_SEL_TGT_CROP:
      subdev_.selection.crop_ = r;
      break;
    case V4L2_SEL_TGT_CROP_DEFAULT:
      subdev_.selection.crop_default_ = r;
      break;
    case V4L2_SEL_TGT_CROP_BOUNDS:
      subdev_.selection.crop_bounds_ = r;
      break;
    case V4L2_SEL_TGT_NATIVE_SIZE:
      subdev_.selection.native_size_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE:
      subdev_.selection.compose_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_DEFAULT:
      subdev_.selection.compose_default_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_BOUNDS:
      subdev_.selection.compose_bounds_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_PADDED:
      subdev_.selection.compose_padded_ = r;
      break;
    default:
      /* Only 8 targets defined as of kernel 6.5 */
      return false;
  }

  struct v4l2_subdev_selection subdev_selection = {};
  subdev_selection.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  subdev_selection.pad = desc_.index;
  subdev_selection.target = target;
  subdev_selection.flags = 0; /* Expect the config to apply precisely */
  subdev_selection.r = r;

  VIDIOC_SUBDEV_S_WRAP(VIDIOC_SUBDEV_S_SELECTION, subdev_selection);
}
