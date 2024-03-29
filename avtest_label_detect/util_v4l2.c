// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>

#include "label_detect.h"

#ifndef V4L2_PIX_FMT_MT2T
#define V4L2_PIX_FMT_MT2T v4l2_fourcc('M', 'T', '2', 'T')
#endif

static bool is_matching_bpp_format(uint32_t fourcc, uint32_t bpp) {
  const uint32_t formats_10bpp[] = {V4L2_PIX_FMT_MT2T};
  const uint32_t formats_8bpp[] = {V4L2_PIX_FMT_MM21};
  bool found = false;
  char fourcc_str[5];
  convert_fourcc_to_str(fourcc, fourcc_str);

  if (bpp == 10) {
    const size_t n = sizeof(formats_10bpp) / sizeof(formats_10bpp[0]);
    for (uint32_t i = 0; i < n; ++i) {
      if (fourcc == formats_10bpp[i]) {
        found = true;
        break;
      }
    }
  } else if (bpp == 8) {
    const size_t n = sizeof(formats_8bpp) / sizeof(formats_8bpp[0]);
    for (uint32_t i = 0; i < n; ++i) {
      if (fourcc == formats_8bpp[i]) {
        found = true;
        break;
      }
    }
  }

  TRACE("is_matching_bpp_format(%s, %dbpp): %s\n", fourcc_str, bpp,
        found ? "true" : "false");

  return found;
}

static bool is_stateless(uint32_t fourcc) {
  const uint32_t stateless_fourcc[] = {
      V4L2_PIX_FMT_AV1_FRAME, V4L2_PIX_FMT_HEVC_SLICE, V4L2_PIX_FMT_H264_SLICE,
      V4L2_PIX_FMT_VP8_FRAME, V4L2_PIX_FMT_VP9_FRAME};
  bool found = false;
  char fourcc_str[5];
  convert_fourcc_to_str(fourcc, fourcc_str);
  TRACE("is_stateless(%s)\n", fourcc_str);

  const size_t n = sizeof(stateless_fourcc) / sizeof(stateless_fourcc[0]);
  for (uint32_t i = 0; i < n; ++i) {
    if (fourcc == stateless_fourcc[i]) {
      found = true;
      break;
    }
  }

  TRACE("is_stateless: %s\n", found ? "true" : "false");
  return found;
}

static bool is_stateless_av1(int fd, uint32_t bpp) {
  struct v4l2_ctrl_av1_sequence params;
  memset(&params, 0, sizeof(params));
  params.bit_depth = bpp;

  struct v4l2_ext_control ext_ctrl;
  memset(&ext_ctrl, 0, sizeof(ext_ctrl));

  ext_ctrl.id = V4L2_CID_STATELESS_AV1_SEQUENCE;
  ext_ctrl.size = sizeof(params);
  ext_ctrl.ptr = &params;

  struct v4l2_ext_controls ext_ctrls;
  memset(&ext_ctrls, 0, sizeof(ext_ctrls));

  ext_ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
  ext_ctrls.request_fd = -1;
  ext_ctrls.count = 1;
  ext_ctrls.controls = &ext_ctrl;

  if (do_ioctl(fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls) == 0) {
    return true;
  }

  return false;
}

static bool is_stateless_decoder_format_supported(int fd,
                                                  uint32_t fourcc,
                                                  uint32_t bpp) {
  bool found = false;

  struct v4l2_format format;
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  format.fmt.pix_mp.pixelformat = fourcc;
  format.fmt.pix_mp.width = 1920;
  format.fmt.pix_mp.height = 1080;
  format.fmt.pix_mp.num_planes = 1;
  format.fmt.pix_mp.plane_fmt[0].sizeimage = 1024 * 1024;

  const int ret = do_ioctl(fd, VIDIOC_S_FMT, &format);
  if ((ret == 0) && (format.fmt.pix_mp.pixelformat == fourcc)) {
    switch (fourcc) {
      case V4L2_PIX_FMT_AV1_FRAME:
        found = is_stateless_av1(fd, bpp);
        break;
      default:
        found = (bpp == 8);
        break;
    }
  }

  if (found) {
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (do_ioctl(fd, VIDIOC_G_FMT, &format) == 0) {
      found = is_matching_bpp_format(format.fmt.pix_mp.pixelformat, bpp);
    }
  }

  return found;
}

bool is_v4l2_support_format(int fd,
                            enum v4l2_buf_type buf_type,
                            uint32_t fourcc,
                            uint32_t bpp) {
  int i;
  bool found = false;
  char fourcc_str[5];

  convert_fourcc_to_str(fourcc, fourcc_str);
  TRACE("is_v4l2_support_format(%s)\n", fourcc_str);
  for (i = 0;; ++i) {
    struct v4l2_fmtdesc format_desc;
    memset(&format_desc, 0, sizeof(format_desc));
    format_desc.type = (enum v4l2_buf_type)buf_type;
    format_desc.index = i;
    if (-1 == do_ioctl(fd, VIDIOC_ENUM_FMT, &format_desc)) {
      break;
    }
    convert_fourcc_to_str(format_desc.pixelformat, fourcc_str);
    TRACE("%s supported\n", fourcc_str);
    if (format_desc.pixelformat == fourcc) {
      found = true;
      /* continue the loop in order to output all supported formats */
    }
  }

  if (found && is_stateless(fourcc)) {
    found = is_stateless_decoder_format_supported(fd, fourcc, bpp);
  } else if (bpp != 8) {
    found = false;
  }

  TRACE("is_v4l2_support_format: %s, %dbpp\n", found ? "true" : "false", bpp);
  return found;
}

/* Returns whether the device fd is V4L2 video encode/decode device. */
bool is_hw_video_acc_device(int fd) {
  struct v4l2_capability cap;
  int ret = do_ioctl(fd, VIDIOC_QUERYCAP, &cap);
  if (ret == 0) {
    if ((cap.capabilities & V4L2_CAP_STREAMING) &&
        (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
      TRACE("is_hw_video_acc_device: true\n");
      return true;
    }
  }
  TRACE("is_hw_video_acc_device: false\n");
  return false;
}

/* Returns whether the device fd is V4L2 jpeg encode/decode device. */
bool is_hw_jpeg_acc_device(int fd) {
  struct v4l2_capability cap;
  int ret = do_ioctl(fd, VIDIOC_QUERYCAP, &cap);
  if (ret == 0) {
    if ((cap.capabilities & V4L2_CAP_STREAMING) &&
        (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
      TRACE("is_hw_jpeg_acc_device: true\n");
      return true;
    }
  }
  TRACE("is_hw_jpeg_acc_device: false\n");
  return false;
}

/* Returns success or failure of getting resolution. The maximum resolution is
   returned through arguments. */
bool get_v4l2_max_resolution(int fd,
                             uint32_t fourcc,
                             int32_t* const resolution_width,
                             int32_t* const resolution_height) {
  *resolution_width = 0;
  *resolution_height = 0;

  struct v4l2_frmsizeenum frame_size;
  memset(&frame_size, 0, sizeof(frame_size));
  frame_size.pixel_format = fourcc;

  for (; do_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0;
       ++frame_size.index) {
    if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      if (frame_size.discrete.width >= *resolution_width &&
          frame_size.discrete.height >= *resolution_height) {
        *resolution_width = frame_size.discrete.width;
        *resolution_height = frame_size.discrete.height;
      }
    } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
               frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
      *resolution_width = frame_size.stepwise.max_width;
      *resolution_height = frame_size.stepwise.max_height;
      break;
    }
  }
  return *resolution_width > 0 && *resolution_height > 0;
}

/* Returns whether a v4l2 encoder driver supports variable bitrate (VBR)
 * encoding
 */
bool is_v4l2_enc_vbr_supported(int fd) {
  struct v4l2_queryctrl query_ctrl;
  memset(&query_ctrl, 0, sizeof(query_ctrl));
  query_ctrl.id = V4L2_CID_MPEG_VIDEO_BITRATE_MODE;
  if (do_ioctl(fd, VIDIOC_QUERYCTRL, &query_ctrl)) {
    return false;
  }

  struct v4l2_querymenu query_menu;
  memset(&query_menu, 0, sizeof(query_menu));
  query_menu.id = query_ctrl.id;
  for (query_menu.index = query_ctrl.minimum;
       (int)query_menu.index <= query_ctrl.maximum; query_menu.index++) {
    if (do_ioctl(fd, VIDIOC_QUERYMENU, &query_menu) == 0) {
      if (query_menu.index == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
        return true;
      }
    }
  }

  return false;
}
