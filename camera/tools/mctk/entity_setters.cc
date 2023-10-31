/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Setters for abstract models of V4L2 entities.
 *
 * If the model has an fd for a kernel device set, then the setters will
 * propagate the new values to the kernel.
 *
 * Return values:
 *  - on success: true
 *  - on failure: false
 */

#include "tools/mctk/entity.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/debug.h"

/* This is a macro instead of a function so we can easily print
 * the ioctl name in the error message.
 */
#define VIDIOC_S_WRAP(ioctl_num, ptr)                          \
  do {                                                         \
    if (this->fd_) {                                           \
      if (ioctl(this->fd_.value(), (ioctl_num), &(ptr)) < 0) { \
        MCTK_PERROR("ioctl(" #ioctl_num ")");                  \
        return false;                                          \
      }                                                        \
    }                                                          \
                                                               \
    return true;                                               \
  } while (0);

bool V4lMcEntity::SetAudio(struct v4l2_audio& audio) {
  maindev_.audio = audio;
  VIDIOC_S_WRAP(VIDIOC_S_AUDIO, audio);
}

bool V4lMcEntity::SetAudout(struct v4l2_audioout& audout) {
  maindev_.audout = audout;
  VIDIOC_S_WRAP(VIDIOC_S_AUDOUT, audout);
}

bool V4lMcEntity::SetCrop(__u32 type, struct v4l2_rect& c) {
  switch (type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      maindev_.crop_video_capture = c;
      break;
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      maindev_.crop_video_output = c;
      break;
    case V4L2_BUF_TYPE_VIDEO_OVERLAY:
      maindev_.crop_video_overlay = c;
      break;
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
      maindev_.crop_video_capture_mplane = c;
      break;
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
      maindev_.crop_video_output_mplane = c;
      break;
    default:
      /* Only 5 types valid for VIDIOC_S_CROP as of kernel 6.5 */
      MCTK_ERR("Not setting cropping rectangle for unknown buffer type.");
      return false;
  }

  struct v4l2_crop crop = {.type = type, .c = c};

  VIDIOC_S_WRAP(VIDIOC_S_CROP, crop);
}

bool V4lMcEntity::SetDvTimings(struct v4l2_dv_timings& dv_timings) {
  maindev_.dv_timings = dv_timings;
  VIDIOC_S_WRAP(VIDIOC_S_DV_TIMINGS, dv_timings);
}

bool V4lMcEntity::SetSubdevDvTimings(
    struct v4l2_dv_timings& subdev_dv_timings) {
  maindev_.subdev_dv_timings = subdev_dv_timings;
  VIDIOC_S_WRAP(VIDIOC_SUBDEV_S_DV_TIMINGS, subdev_dv_timings);
}

bool V4lMcEntity::SetFbuf(struct v4l2_framebuffer& fbuf) {
  maindev_.fbuf = fbuf;
  VIDIOC_S_WRAP(VIDIOC_S_FBUF, fbuf);
}

bool V4lMcEntity::SetFmtVideoCapture(struct v4l2_pix_format& pix) {
  maindev_.fmt_video_capture = pix;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, format.fmt.pix = pix;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVideoOutput(struct v4l2_pix_format& pix) {
  maindev_.fmt_video_output = pix;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT, format.fmt.pix = pix;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVideoOverlay(struct v4l2_window& win) {
  maindev_.fmt_video_overlay = win;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY, format.fmt.win = win;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVbiCapture(struct v4l2_vbi_format& vbi) {
  maindev_.fmt_vbi_capture = vbi;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VBI_CAPTURE, format.fmt.vbi = vbi;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVbiOutput(struct v4l2_vbi_format& vbi) {
  maindev_.fmt_vbi_output = vbi;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VBI_OUTPUT, format.fmt.vbi = vbi;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtSlicedVbiCapture(
    struct v4l2_sliced_vbi_format& sliced) {
  maindev_.fmt_sliced_vbi_capture = sliced;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE, format.fmt.sliced = sliced;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtSlicedVbiOutput(struct v4l2_sliced_vbi_format& sliced) {
  maindev_.fmt_sliced_vbi_output = sliced;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_SLICED_VBI_OUTPUT, format.fmt.sliced = sliced;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVideoOutputOverlay(struct v4l2_window& win) {
  maindev_.fmt_video_output_overlay = win;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY, format.fmt.win = win;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVideoCaptureMplane(
    struct v4l2_pix_format_mplane& pix_mp) {
  maindev_.fmt_video_capture_mplane = pix_mp;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, format.fmt.pix_mp = pix_mp;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtVideoOutputMplane(
    struct v4l2_pix_format_mplane& pix_mp) {
  maindev_.fmt_video_output_mplane = pix_mp;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, format.fmt.pix_mp = pix_mp;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtSdrCapture(struct v4l2_sdr_format& sdr) {
  maindev_.fmt_sdr_capture = sdr;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_SDR_CAPTURE, format.fmt.sdr = sdr;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtSdrOutput(struct v4l2_sdr_format& sdr) {
  maindev_.fmt_sdr_output = sdr;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_SDR_OUTPUT, format.fmt.sdr = sdr;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtMetaCapture(struct v4l2_meta_format& meta) {
  maindev_.fmt_meta_capture = meta;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_META_CAPTURE, format.fmt.meta = meta;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetFmtMetaOutput(struct v4l2_meta_format& meta) {
  maindev_.fmt_meta_output = meta;

  struct v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_META_OUTPUT, format.fmt.meta = meta;

  VIDIOC_S_WRAP(VIDIOC_S_FMT, format);
}

bool V4lMcEntity::SetInput(int input) {
  maindev_.input = input;
  VIDIOC_S_WRAP(VIDIOC_S_INPUT, input);
}

bool V4lMcEntity::SetJpegcomp(struct v4l2_jpegcompression& jpegcomp) {
  maindev_.jpegcomp = jpegcomp;
  VIDIOC_S_WRAP(VIDIOC_S_JPEGCOMP, jpegcomp);
}

bool V4lMcEntity::SetOutput(int output) {
  maindev_.output = output;
  VIDIOC_S_WRAP(VIDIOC_S_OUTPUT, output);
}

bool V4lMcEntity::SetParmVideoCapture(struct v4l2_captureparm& capture) {
  maindev_.parm_video_capture = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVideoOutput(struct v4l2_outputparm& output) {
  maindev_.parm_video_output = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT, streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVideoOverlay(struct v4l2_outputparm& output) {
  maindev_.parm_video_overlay = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_OVERLAY,
  streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVbiCapture(struct v4l2_captureparm& capture) {
  maindev_.parm_vbi_capture = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VBI_CAPTURE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVbiOutput(struct v4l2_outputparm& output) {
  maindev_.parm_vbi_output = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VBI_OUTPUT, streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmSlicedVbiCapture(struct v4l2_captureparm& capture) {
  maindev_.parm_sliced_vbi_capture = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmSlicedVbiOutput(struct v4l2_outputparm& output) {
  maindev_.parm_sliced_vbi_output = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_SLICED_VBI_OUTPUT,
  streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVideoOutputOverlay(struct v4l2_outputparm& output) {
  maindev_.parm_video_output_overlay = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY,
  streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVideoCaptureMplane(struct v4l2_captureparm& capture) {
  maindev_.parm_video_capture_mplane = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmVideoOutputMplane(struct v4l2_outputparm& output) {
  maindev_.parm_video_output_mplane = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
  streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmSdrCapture(struct v4l2_captureparm& capture) {
  maindev_.parm_sdr_capture = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_SDR_CAPTURE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmSdrOutput(struct v4l2_outputparm& output) {
  maindev_.parm_sdr_output = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_SDR_OUTPUT, streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmMetaCapture(struct v4l2_captureparm& capture) {
  maindev_.parm_meta_capture = capture;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_META_CAPTURE,
  streamparm.parm.capture = capture;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetParmMetaOutput(struct v4l2_outputparm& output) {
  maindev_.parm_meta_output = output;

  struct v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_META_OUTPUT, streamparm.parm.output = output;

  VIDIOC_S_WRAP(VIDIOC_S_PARM, streamparm);
}

bool V4lMcEntity::SetPriority(enum v4l2_priority priority) {
  maindev_.priority = priority;
  VIDIOC_S_WRAP(VIDIOC_S_PRIORITY, priority);
}

bool V4lMcEntity::SetSelection(__u32 type, __u32 target, struct v4l2_rect& r) {
  if (type < V4L2_BUF_TYPE_VIDEO_CAPTURE || type > V4L2_BUF_TYPE_META_OUTPUT)
    /* Only 14 buffer types defined as of kernel 6.5 */
    return false;

  switch (target) {
    case V4L2_SEL_TGT_CROP:
      maindev_.selection[type - 1].crop_ = r;
      break;
    case V4L2_SEL_TGT_CROP_DEFAULT:
      maindev_.selection[type - 1].crop_default_ = r;
      break;
    case V4L2_SEL_TGT_CROP_BOUNDS:
      maindev_.selection[type - 1].crop_bounds_ = r;
      break;
    case V4L2_SEL_TGT_NATIVE_SIZE:
      maindev_.selection[type - 1].native_size_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE:
      maindev_.selection[type - 1].compose_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_DEFAULT:
      maindev_.selection[type - 1].compose_default_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_BOUNDS:
      maindev_.selection[type - 1].compose_bounds_ = r;
      break;
    case V4L2_SEL_TGT_COMPOSE_PADDED:
      maindev_.selection[type - 1].compose_padded_ = r;
      break;
    default:
      /* Only 8 targets defined as of kernel 6.5 */
      return false;
  }

  struct v4l2_selection selection = {};
  selection.type = type, selection.target = target;
  selection.flags = 0; /* Expect the config to apply precisely */
  selection.r = r;

  VIDIOC_S_WRAP(VIDIOC_S_SELECTION, selection);
}

bool V4lMcEntity::SetStd(v4l2_std_id std) {
  maindev_.std = std;
  VIDIOC_S_WRAP(VIDIOC_S_STD, std);
}

bool V4lMcEntity::SetSubdevStd(v4l2_std_id subdev_std) {
  maindev_.subdev_std = subdev_std;
#ifdef VIDIOC_SUBDEV_S_STD
  VIDIOC_S_WRAP(VIDIOC_SUBDEV_S_STD, subdev_std);
#else
  return true;
#endif /* VIDIOC_SUBDEV_S_STD */
}
