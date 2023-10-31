/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media-ctl pad.
 * It will be populated with data from a kernel device.
 *
 * The resulting model will keep accessing the fd to the V4L2 device.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 pad.
 *  - on failure: nullptr.
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <memory>
#include <optional>

#include "tools/mctk/debug.h"
#include "tools/mctk/entity.h"
#include "tools/mctk/pad.h"

std::unique_ptr<V4lMcPad> V4lMcPad::CreateFromKernel(
    struct media_pad_desc& desc,
    V4lMcEntity& entity,
    std::optional<int> fd_ent) {
  auto pad = std::make_unique<V4lMcPad>(entity, fd_ent);

  pad->desc_ = desc;

  /* If the pad is part of an entity without a /dev/videoX or /dev/v4l-subdevX
   * device, then there is nothing for us to ioctl() on - we're done.
   */
  if (!fd_ent)
    return pad;

  MCTK_ASSERT(*fd_ent >= 0);

  pad->fd_ent_ = *fd_ent;

  struct v4l2_subdev_crop crop = {};
  crop.pad = pad->desc_.index;
  crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  if (ioctl(*fd_ent, VIDIOC_SUBDEV_G_CROP, &crop) >= 0)
    pad->subdev_.crop = crop.rect;

  struct v4l2_subdev_format format = {};
  format.pad = pad->desc_.index;
  format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  if (ioctl(*fd_ent, VIDIOC_SUBDEV_G_FMT, &format) >= 0)
    pad->subdev_.fmt = format.format;

  struct v4l2_subdev_frame_interval interval = {};
  interval.pad = pad->desc_.index;
  if (ioctl(*fd_ent, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &interval) >= 0)
    pad->subdev_.frame_interval = interval.interval;

#define QUERY_SUBDEV_SELECTION(tgt, dest)                       \
  do {                                                          \
    struct v4l2_subdev_selection query = {};                    \
    query.which = V4L2_SUBDEV_FORMAT_ACTIVE;                    \
    query.pad = pad->desc_.index;                               \
    query.flags = 0;                                            \
    query.target = tgt;                                         \
    if (ioctl(*fd_ent, VIDIOC_SUBDEV_G_SELECTION, &query) >= 0) \
      pad->subdev_.selection.dest = query.r;                    \
  } while (0);

  /* Not all targets are valid for subdevs, but this has already
   * changed once, so let's query them all to be future proof.
   */
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_CROP, crop_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_CROP_DEFAULT, crop_default_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_CROP_BOUNDS, crop_bounds_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_NATIVE_SIZE, native_size_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_COMPOSE, compose_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_COMPOSE_DEFAULT, compose_default_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_COMPOSE_BOUNDS, compose_bounds_);
  QUERY_SUBDEV_SELECTION(V4L2_SEL_TGT_COMPOSE_PADDED, compose_padded_);

  return pad;
}
