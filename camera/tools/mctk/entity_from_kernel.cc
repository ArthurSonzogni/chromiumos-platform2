/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media-ctl entity.
 * It will be populated with data from a kernel device.
 *
 * The resulting model will own the fd to the V4L2 device.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 entity.
 *  - on failure: nullptr.
 */

#include "tools/mctk/entity.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h> /* PATH_MAX */
#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"
#include "tools/mctk/yaml_tree.h"

namespace {

/* Helper to convert a V4L character node (major:minor tuple) to a nice
 * device path that is easily recognised by users.
 *
 * For example, (81, 0) is converted to:
 * - "/dev/char/81:0" (via string concatenation)
 * - "../video0" (via readlink())
 * - "/dev/video0" (via string concatenation)
 */
std::string DevNodeFromDevNum(unsigned int maj, unsigned int min) {
  /* Convert (81, 0) into "/dev/char/81:0" */
  std::string dev_char =
      "/dev/char/" + std::to_string(maj) + ":" + std::to_string(min);

  /* Get a path such as ../video16 */
  char dev_link[PATH_MAX];
  ssize_t len = readlink(dev_char.c_str(), dev_link, sizeof(dev_link));
  if (len < 0) {
    /* Device may simply not exist. Some entities do not spawn device files.
     * Whatever the error, return an empty string.
     */
    return std::string();
  } else if (len >= (signed)sizeof(dev_link)) {
    /* Truncation likely happened.
     * Return numbered device instead.
     */
    return dev_char;
  }

  /* readlink() does not add a NUL byte. */
  dev_link[len] = '\0';

  /* Replace ../ with /dev/ by skipping over the first 3 bytes */
  return "/dev/" + std::string(&dev_link[3]);
}

void QueryV4LPropsFromKernel(V4lMcEntity& entity, int fd_ent) {
  /* Query entity's "regular" V4L2 properties */

#define QUERY_V4L_INT(ioctl_name, dest)        \
  do {                                         \
    int temp;                                  \
    if (ioctl(fd_ent, ioctl_name, &temp) >= 0) \
      entity.maindev_.dest = temp;             \
  } while (0);

#define QUERY_V4L_PRIORITY(ioctl_name, dest)   \
  do {                                         \
    v4l2_priority temp;                        \
    if (ioctl(fd_ent, ioctl_name, &temp) >= 0) \
      entity.maindev_.dest = temp;             \
  } while (0);

#define QUERY_V4L_STRUCT(ioctl_name, dest, struct_type) \
  do {                                                  \
    struct struct_type temp = {};                       \
    if (ioctl(fd_ent, ioctl_name, &temp) >= 0)          \
      entity.maindev_.dest = temp;                      \
  } while (0);

// MCTK_ASSERT(std::is_same<struct copy_type,
//                          decltype(query.query_member)>::value);
#define QUERY_V4L_STRUCT_TYPE(ioctl_name, subtype, dest, query_type, \
                              query_member, copy_type)               \
  do {                                                               \
    struct query_type query = {};                                    \
    query.type = subtype;                                            \
    if (ioctl(fd_ent, ioctl_name, &query) >= 0)                      \
      entity.maindev_.dest = query.query_member;                     \
  } while (0);

#define QUERY_V4L_SELECTION(buftype, tgt, dest)              \
  do {                                                       \
    entity.maindev_.selection[buftype - 1].dest.reset();     \
    struct v4l2_selection query = {};                        \
    query.type = buftype;                                    \
    query.target = tgt;                                      \
    if (ioctl(fd_ent, VIDIOC_G_SELECTION, &query) >= 0)      \
      entity.maindev_.selection[buftype - 1].dest = query.r; \
  } while (0);

  QUERY_V4L_STRUCT(VIDIOC_G_AUDIO, audio, v4l2_audio);

  QUERY_V4L_STRUCT(VIDIOC_G_AUDOUT, audout, v4l2_audioout);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_CROP, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                        crop_video_capture, v4l2_crop, c, v4l2_rect);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_CROP, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                        crop_video_output, v4l2_crop, c, v4l2_rect);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_CROP, V4L2_BUF_TYPE_VIDEO_OVERLAY,
                        crop_video_overlay, v4l2_crop, c, v4l2_rect);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_CROP, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                        crop_video_capture_mplane, v4l2_crop, c, v4l2_rect);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_CROP, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                        crop_video_output_mplane, v4l2_crop, c, v4l2_rect);

  /* Ignored: VIDIOC_G_CTRL */
  /* We do VIDIOC_G_EXT_CTRLS instead */

  QUERY_V4L_STRUCT(VIDIOC_G_DV_TIMINGS, dv_timings, v4l2_dv_timings);

  QUERY_V4L_STRUCT(VIDIOC_SUBDEV_G_DV_TIMINGS, subdev_dv_timings,
                   v4l2_dv_timings);

  /* Ignored: VIDIOC_G_EDID */
  /* Ignored: VIDIOC_SUBDEV_G_EDID */

  /* Ignored: VIDIOC_G_ENC_INDEX */
  /* Irrelevant: Outdated and not a device configuration */

  /* VIDIOC_G_EXT_CTRLS done separately */

  QUERY_V4L_STRUCT(VIDIOC_G_FBUF, fbuf, v4l2_framebuffer);

  /* VIDIOC_G_FMT */
  /* This ioctl() handles a different struct for each v4l2_buf_type,
   * hence the seemingly duplicated code - it is not.
   */
  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_OVERLAY,
                        fmt_video_capture, v4l2_format, fmt.pix,
                        v4l2_pix_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                        fmt_video_output, v4l2_format, fmt.pix,
                        v4l2_pix_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_OVERLAY,
                        fmt_video_overlay, v4l2_format, fmt.win, v4l2_window);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VBI_CAPTURE,
                        fmt_vbi_capture, v4l2_format, fmt.vbi, v4l2_vbi_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VBI_OUTPUT, fmt_vbi_output,
                        v4l2_format, fmt.vbi, v4l2_vbi_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_SLICED_VBI_CAPTURE,
                        fmt_sliced_vbi_capture, v4l2_format, fmt.sliced,
                        v4l2_sliced_vbi_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_SLICED_VBI_OUTPUT,
                        fmt_sliced_vbi_output, v4l2_format, fmt.sliced,
                        v4l2_sliced_vbi_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY,
                        fmt_video_output_overlay, v4l2_format, fmt.win,
                        v4l2_window);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                        fmt_video_capture_mplane, v4l2_format, fmt.pix_mp,
                        v4l2_pix_format_mplane);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                        fmt_video_output_mplane, v4l2_format, fmt.pix_mp,
                        v4l2_pix_format_mplane);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_SDR_CAPTURE,
                        fmt_sdr_capture, v4l2_format, fmt.sdr, v4l2_sdr_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_SDR_OUTPUT, fmt_sdr_output,
                        v4l2_format, fmt.sdr, v4l2_sdr_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_META_CAPTURE,
                        fmt_meta_capture, v4l2_format, fmt.meta,
                        v4l2_meta_format);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_FMT, V4L2_BUF_TYPE_META_OUTPUT,
                        fmt_meta_output, v4l2_format, fmt.meta,
                        v4l2_meta_format);

  /* Ignored: VIDIOC_G_FREQUENCY */

  QUERY_V4L_INT(VIDIOC_G_INPUT, input);

  QUERY_V4L_STRUCT(VIDIOC_G_JPEGCOMP, jpegcomp, v4l2_jpegcompression);

  /* Ignored: VIDIOC_G_MODULATOR */
  for (__u32 i = 0; /*pass*/; i++) {
    struct v4l2_modulator modulator = {};
    modulator.index = i;

    if (ioctl(fd_ent, VIDIOC_G_MODULATOR, &modulator) < 0)
      break;

    entity.maindev_.modulators.push_back(modulator);
  }

  QUERY_V4L_INT(VIDIOC_G_OUTPUT, output);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                        parm_video_capture, v4l2_streamparm, parm.capture,
                        v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                        parm_video_output, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_OVERLAY,
                        parm_video_overlay, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VBI_CAPTURE,
                        parm_vbi_capture, v4l2_streamparm, parm.capture,
                        v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VBI_OUTPUT,
                        parm_vbi_output, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_SLICED_VBI_CAPTURE,
                        parm_sliced_vbi_capture, v4l2_streamparm, parm.capture,
                        v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_SLICED_VBI_OUTPUT,
                        parm_sliced_vbi_output, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY,
                        parm_video_output_overlay, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                        parm_video_capture_mplane, v4l2_streamparm,
                        parm.capture, v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                        parm_video_output_mplane, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_SDR_CAPTURE,
                        parm_sdr_capture, v4l2_streamparm, parm.capture,
                        v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_SDR_OUTPUT,
                        parm_sdr_output, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_META_CAPTURE,
                        parm_meta_capture, v4l2_streamparm, parm.capture,
                        v4l2_captureparm);

  QUERY_V4L_STRUCT_TYPE(VIDIOC_G_PARM, V4L2_BUF_TYPE_META_OUTPUT,
                        parm_meta_output, v4l2_streamparm, parm.output,
                        v4l2_outputparm);

  QUERY_V4L_PRIORITY(VIDIOC_G_PRIORITY, priority);

  /* VIDIOC_G_SELECTION */
  for (int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       type <= V4L2_BUF_TYPE_META_OUTPUT; type++) {
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_CROP, crop_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_CROP_DEFAULT, crop_default_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_CROP_BOUNDS, crop_bounds_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_NATIVE_SIZE, native_size_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_COMPOSE, compose_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_COMPOSE_DEFAULT, compose_default_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_COMPOSE_BOUNDS, compose_bounds_);
    QUERY_V4L_SELECTION(type, V4L2_SEL_TGT_COMPOSE_PADDED, compose_padded_);
  }

  QUERY_V4L_INT(VIDIOC_G_STD, std);

#ifdef VIDIOC_SUBDEV_S_STD
  QUERY_V4L_INT(VIDIOC_SUBDEV_G_STD, subdev_std);
#endif /* VIDIOC_SUBDEV_S_STD */

  /* Ignored: VIDIOC_G_TUNER */
  for (__u32 i = 0; /*pass*/; i++) {
    struct v4l2_tuner tuner = {};
    tuner.index = i;

    if (ioctl(fd_ent, VIDIOC_G_TUNER, &tuner) < 0)
      break;

    entity.maindev_.tuners.push_back(tuner);
  }
}

}  // namespace

std::unique_ptr<V4lMcEntity> V4lMcEntity::CreateFromKernel(
    struct media_entity_desc& desc, int fd_mc) {
  MCTK_ASSERT(fd_mc >= 0);

  auto entity = std::make_unique<V4lMcEntity>();

  entity->desc_ = desc;

  /* Allocate temporary arrays for the ioctl ABI */
  struct media_links_enum links_enum;
  struct media_pad_desc kernel_pads[entity->desc_.pads];
  struct media_link_desc kernel_links[entity->desc_.links];

  links_enum.entity = entity->desc_.id;
  links_enum.pads = kernel_pads;
  links_enum.links = kernel_links;

  if (ioctl(fd_mc, MEDIA_IOC_ENUM_LINKS, &links_enum) < 0) {
    MCTK_PERROR("ioctl(MEDIA_IOC_ENUM_LINKS)");
    return nullptr;
  }

  /* Some entities don't create /dev/v4l-subdevX files.
   * If this entity has one, keep an fd open.
   */
  entity->devpath_ =
      DevNodeFromDevNum(entity->desc_.dev.major, entity->desc_.dev.minor);
  if (entity->devpath_.size()) {
    int fd_ent = open(entity->devpath_.c_str(), O_RDWR);
    if (fd_ent < 0)
      return nullptr;

    entity->fd_ = fd_ent;
  }

  /* Copy pads from the kernel populated array and instantiate our classes */
  for (size_t i = 0; i < entity->desc_.pads; i++) {
    std::unique_ptr<V4lMcPad> pad =
        V4lMcPad::CreateFromKernel(links_enum.pads[i], *entity, entity->fd_);

    if (!pad)
      return nullptr;

    entity->pads_.push_back(std::move(pad));
  }

  /* Copy links from the kernel populated array and instantiate our classes */
  for (size_t i = 0; i < entity->desc_.links; i++) {
    /* Ignore incoming links.
     * For each entity, we only store the outgoing links.
     */
    if (links_enum.links[i].source.entity != entity->desc_.id)
      continue;

    auto link = std::make_unique<V4lMcLink>(fd_mc);

    link->desc_ = links_enum.links[i];

    entity->links_.push_back(std::move(link));
  }

  /* Query classic V4L properties */
  if (entity->fd_)
    QueryV4LPropsFromKernel(*entity, *entity->fd_);

  /* Query controls */
  if (entity->fd_) {
    for (__u32 id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
         /* pass */ true;
         id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND) {
      struct v4l2_query_ext_ctrl qec = {};
      qec.id = id;

      int ret = ioctl(*entity->fd_, VIDIOC_QUERY_EXT_CTRL, &qec);
      if (ret) {
        /* Done enumerating? */
        if (errno == EINVAL)
          break;

        /* Does this device even have (extended) controls? */
        if (errno == ENOTTY)
          break;

        /* Some error happened */
        MCTK_PANIC("VIDIOC_QUERY_EXT_CTRL");
        break;
      }

      /* Feed the ID back to the loop, otherwise it won't stop */
      id = qec.id;

      if (qec.type == V4L2_CTRL_TYPE_CTRL_CLASS)
        continue;

      if (qec.flags & V4L2_CTRL_FLAG_DISABLED)
        continue;

      std::unique_ptr<V4lMcControl> control =
          V4lMcControl::CreateFromKernel(qec, *entity->fd_);
      if (!control) {
        MCTK_ERR("Failed to read control from Kernel - skipping.");
        continue;
      }

      entity->controls_.push_back(std::move(control));
    }
  }

  return entity;
}
