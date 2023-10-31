/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wrapper class capturing a snapshot of the description and properties of
 * a media-ctl "pad" on a V4L2 subdevice.
 *
 * Setter functions primarily update the state in the class.
 * If fd_ent_ is set to an fd to the V4L2 subdevice, the matching ioctl()s
 * are sent to the kernel, programming the updated values into the driver.
 *
 * If fd_ent_ is set, this class DOES NOT own it and will NOT close it.
 */

#ifndef CAMERA_TOOLS_MCTK_PAD_H_
#define CAMERA_TOOLS_MCTK_PAD_H_

#include <linux/media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <memory>
#include <optional>
#include <vector>

#include "tools/mctk/selection.h"
#include "tools/mctk/yaml_tree.h"

class V4lMcEntity;
class V4lMcLink;

class V4lMcPad {
 public:
  /* Public functions */

  /* This constructor should be private, but that forces hacks to make
   * unique_ptr work. So let's keep it public, but please use the
   * factory functions instead of the constructor directly!
   */
  V4lMcPad(V4lMcEntity& entity, std::optional<int> fd_ent)
      : desc_({}), entity_(entity), fd_ent_(fd_ent) {}

  V4lMcLink* LinkBySinkIds(__u32 entity, __u16 index);

  /* Factory functions */
  static std::unique_ptr<V4lMcPad> CreateFromKernel(struct media_pad_desc& desc,
                                                    V4lMcEntity& entity,
                                                    std::optional<int> fd_ent);
  static std::unique_ptr<V4lMcPad> CreateFromYamlNode(YamlNode& node_pad,
                                                      V4lMcEntity& entity);

  /* Setters for V4L2 subdev properties */
  bool SetCrop(struct v4l2_rect& crop);
  bool SetFmt(struct v4l2_mbus_framefmt& fmt);
  bool SetFrameInterval(struct v4l2_fract& frame_interval);
  bool SetSelection(__u32 target, struct v4l2_rect& r);

  /* Public variables */

  /* Pad description, as per MEDIA_IOC_ENUM_LINKS */
  struct media_pad_desc desc_;

  /* V4L2 subdev properties */
  struct {
    std::optional<struct v4l2_rect> crop;
    std::optional<struct v4l2_mbus_framefmt> fmt;
    std::optional<struct v4l2_fract> frame_interval;

    V4lMcSelection selection;
  } subdev_;

  V4lMcEntity& entity_;
  std::vector<V4lMcLink*> links_;

 private:
  /* Private variables */

  /* Optional fd to V4L2 subdevice containing this pad.
   * If this is set, setters will additionally call ioctl() on this fd.
   */
  std::optional<int> fd_ent_;
};

#endif /* CAMERA_TOOLS_MCTK_PAD_H_ */
