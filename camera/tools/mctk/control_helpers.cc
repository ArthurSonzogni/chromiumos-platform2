/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/control_helpers.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */

#include "tools/mctk/debug.h"

size_t ControlHelperElemSize(__u32 type_u32) {
  enum v4l2_ctrl_type type = (enum v4l2_ctrl_type)type_u32;

  switch (type) {
    case V4L2_CTRL_TYPE_INTEGER:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_BOOLEAN:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_MENU:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_BUTTON:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_INTEGER64:
      return sizeof(__s64);
    case V4L2_CTRL_TYPE_CTRL_CLASS:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_STRING:
      /* elem_size has a different meaning for V4L2_CTRL_TYPE_STRING */
      MCTK_PANIC("ControlHelperElemSize() is not defined for string controls");
      break;
    case V4L2_CTRL_TYPE_BITMASK:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_INTEGER_MENU:
      return sizeof(__s32);
    case V4L2_CTRL_TYPE_U8:
      return sizeof(__u8);
    case V4L2_CTRL_TYPE_U16:
      return sizeof(__u16);
    case V4L2_CTRL_TYPE_U32:
      return sizeof(__u32);
#ifdef V4L2_CTRL_TYPE_AREA
    case V4L2_CTRL_TYPE_AREA:
      return sizeof(struct v4l2_area);
#endif /* V4L2_CTRL_TYPE_AREA */
#ifdef V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS
    case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
      return sizeof(struct v4l2_ctrl_hdr10_cll_info);
    case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
      return sizeof(struct v4l2_ctrl_hdr10_mastering_display);
    case V4L2_CTRL_TYPE_H264_SPS:
      return sizeof(struct v4l2_ctrl_h264_sps);
    case V4L2_CTRL_TYPE_H264_PPS:
      return sizeof(struct v4l2_ctrl_h264_pps);
    case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
      return sizeof(struct v4l2_ctrl_h264_scaling_matrix);
    case V4L2_CTRL_TYPE_H264_SLICE_PARAMS:
      return sizeof(struct v4l2_ctrl_h264_slice_params);
    case V4L2_CTRL_TYPE_H264_DECODE_PARAMS:
      return sizeof(struct v4l2_ctrl_h264_decode_params);
    case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS:
      return sizeof(struct v4l2_ctrl_h264_pred_weights);
    case V4L2_CTRL_TYPE_FWHT_PARAMS:
      return sizeof(struct v4l2_ctrl_fwht_params);
    case V4L2_CTRL_TYPE_VP8_FRAME:
      return sizeof(struct v4l2_ctrl_vp8_frame);
    case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
      return sizeof(struct v4l2_ctrl_mpeg2_quantisation);
    case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
      return sizeof(struct v4l2_ctrl_mpeg2_sequence);
    case V4L2_CTRL_TYPE_MPEG2_PICTURE:
      return sizeof(struct v4l2_ctrl_mpeg2_picture);
    case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR:
      return sizeof(struct v4l2_ctrl_vp9_compressed_hdr);
    case V4L2_CTRL_TYPE_VP9_FRAME:
      return sizeof(struct v4l2_ctrl_vp9_frame);

    case V4L2_CTRL_TYPE_HEVC_SPS:
      return sizeof(struct v4l2_ctrl_hevc_sps);
    case V4L2_CTRL_TYPE_HEVC_PPS:
      return sizeof(struct v4l2_ctrl_hevc_pps);
    case V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS:
      return sizeof(struct v4l2_ctrl_hevc_slice_params);
    case V4L2_CTRL_TYPE_HEVC_SCALING_MATRIX:
      return sizeof(struct v4l2_ctrl_hevc_scaling_matrix);
    case V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS:
      return sizeof(struct v4l2_ctrl_hevc_decode_params);
#endif /* V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS */
#ifdef V4L2_CTRL_TYPE_AV1_FILM_GRAIN
    case V4L2_CTRL_TYPE_AV1_SEQUENCE:
      return sizeof(struct v4l2_ctrl_av1_sequence);
    case V4L2_CTRL_TYPE_AV1_TILE_GROUP_ENTRY:
      return sizeof(struct v4l2_ctrl_av1_tile_group_entry);
    case V4L2_CTRL_TYPE_AV1_FRAME:
      return sizeof(struct v4l2_ctrl_av1_frame);
    case V4L2_CTRL_TYPE_AV1_FILM_GRAIN:
      return sizeof(struct v4l2_ctrl_av1_film_grain);
#endif /* V4L2_CTRL_TYPE_AV1_FILM_GRAIN */
    default:
      MCTK_PANIC("Unknown control type");
  }
}

bool ControlHelperDescLooksOK(struct v4l2_query_ext_ctrl& desc) {
  /* Check desc.
   * An error here indicates either of:
   *  - a misinterpretation of the V4L2 API,
   *  - an ABI mismatch,
   *  - or a bug in the kernel.
   */
  if (desc.nr_of_dims > V4L2_CTRL_MAX_DIMS) {
    MCTK_ERR("nr_of_dims > V4L2_CTRL_MAX_DIMS");
    return false;
  }

  if (desc.flags & V4L2_CTRL_FLAG_DYNAMIC_ARRAY) {
    /* Dynamically sized 1-dimensional array */
    if (desc.nr_of_dims != 1) {
      MCTK_ERR("Dynamically sized array with nr_of_dims != 1.");
      return false;
    }

    if (desc.elems < 1 || desc.elems > desc.dims[0]) {
      MCTK_ERR("Dynamically sized array elems out of bounds.");
      return false;
    }
  } else if (desc.nr_of_dims > 0) {
    /* This is an array */
    __u32 temp = 1;
    for (size_t i = 0; i < desc.nr_of_dims; i++)
      temp *= desc.dims[i];

    if (temp != desc.elems) {
      MCTK_ERR("Array description and number of elements do not match.");
      return false;
    }
  } else {
    /* Not an array */
    if (desc.elems != 1) {
      MCTK_ERR("Non-array control claims to have more than one element.");
      return false;
    }
  }

  /* v4l2_ctrl_new() should ensure this - see linux/.../v4l2-ctrls-core.c */
  if (desc.type >= V4L2_CTRL_COMPOUND_TYPES &&
      !(desc.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD)) {
    MCTK_ERR("Compound type without payload.");
    return false;
  }

  /* v4l2_ctrl_new() should ensure this - see linux/.../v4l2-ctrls-core.c */
  if (desc.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD &&
      desc.type != V4L2_CTRL_TYPE_STRING &&
      ControlHelperElemSize(desc.type) != desc.elem_size) {
    MCTK_ERR("Payload element size does not match type.");
    return false;
  }

  /* We could do some more checks for minimum/maximum values here, but
   * let's blindly trust the user for now.
   * We just need to check the most important things to ensure we have
   * a correct understanding of the TYPES of values we're handling.
   */

  return true;
}
