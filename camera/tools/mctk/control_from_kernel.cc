/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 control.
 * It will be populated with data from a kernel device.
 *
 * The resulting model will keep accessing the fd to the V4L2 device.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 control.
 *  - on failure: nullptr.
 */

#include "tools/mctk/control.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */
#include <sys/ioctl.h>

#include <memory>

#include "tools/mctk/control_helpers.h"
#include "tools/mctk/debug.h"

std::unique_ptr<V4lMcControl> V4lMcControl::CreateFromKernel(
    struct v4l2_query_ext_ctrl& desc, int fd_ent) {
  MCTK_ASSERT(fd_ent >= 0);

  if (!ControlHelperDescLooksOK(desc)) {
    MCTK_ERR("Control description doesn't look right, aborting.");
    return nullptr;
  }

  /* Looking good, let's query the control. */
  auto control = std::make_unique<V4lMcControl>(fd_ent);

  /* Store the control's metadata */
  control->desc_ = desc;

  /* Temporary structs for ioctl() */
  struct v4l2_ext_control kernel_values = {};
  kernel_values.id = desc.id;
  kernel_values.size = 0;

  struct v4l2_ext_controls request = {};
  request.which = V4L2_CTRL_WHICH_CUR_VAL;
  request.count = 1;
  request.controls = &kernel_values;

  /* There are controls with just one s32 or s64 values.
   * Others have an array of values, strings, or structs as a "payload".
   */
  if (!(desc.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD)) {
    /* This is just a simple __s32 or __s64 value */

    if (ioctl(fd_ent, VIDIOC_G_EXT_CTRLS, &request) < 0) {
      MCTK_PERROR("VIDIOC_G_EXT_CTRLS for simple value");
      return nullptr;
    }

    if (desc.type == V4L2_CTRL_TYPE_INTEGER64)
      control->values_s64_.push_back(kernel_values.value64);
    else
      control->values_s32_.push_back(kernel_values.value);
  } else {
    /* Complex data type - need to query size and allocate buffer */

    /* Query size first. */
    if (ioctl(fd_ent, VIDIOC_G_EXT_CTRLS, &request) < 0 && errno != ENOSPC) {
      MCTK_PERROR("VIDIOC_G_EXT_CTRLS for payload size");
      return nullptr;
    }

    /* Check sizes.
     * An error here indicates either of:
     *  - a misinterpretation of the V4L2 API,
     *  - an ABI mismatch,
     *  - or a bug in the kernel.
     */
    if (kernel_values.size != desc.elem_size * desc.elems) {
      MCTK_ERR("Buffer size and element size*count do not match.");
      return nullptr;
    }

    /* Allocate buffer and prepare to pass it to ioctl() */
    std::vector<__u8> payload;
    payload.resize(kernel_values.size);
    kernel_values.ptr = payload.data();

    /* Retry query, this time with a valid buffer pointer */
    if (ioctl(fd_ent, VIDIOC_G_EXT_CTRLS, &request) < 0) {
      MCTK_PERROR("VIDIOC_G_EXT_CTRLS for payload data");
      return nullptr;
    }

    /* Copy values to vectors, making them easily C++ accessible */
    switch (desc.type) {
      case V4L2_CTRL_TYPE_INTEGER:
      case V4L2_CTRL_TYPE_BOOLEAN:
      case V4L2_CTRL_TYPE_MENU:
      case V4L2_CTRL_TYPE_BUTTON:
      case V4L2_CTRL_TYPE_BITMASK:
      case V4L2_CTRL_TYPE_INTEGER_MENU:
        control->values_s32_.resize(desc.elems);
        memcpy(control->values_s32_.data(), payload.data(), kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_INTEGER64:
        control->values_s64_.resize(desc.elems);
        memcpy(control->values_s64_.data(), payload.data(), kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_STRING:
        control->values_string_.resize(desc.elems);
        for (size_t i = 0; i < desc.elems; i++) {
          __u8* tmp = &payload.data()[i * desc.elem_size];
          control->values_string_[i] =
              std::string(reinterpret_cast<char*>(tmp));
        }
        break;
      case V4L2_CTRL_TYPE_U8:
        control->values_u8_.resize(desc.elems);
        memcpy(control->values_u8_.data(), payload.data(), kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_U16:
        control->values_u16_.resize(desc.elems);
        memcpy(control->values_u16_.data(), payload.data(), kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_U32:
        control->values_u32_.resize(desc.elems);
        memcpy(control->values_u32_.data(), payload.data(), kernel_values.size);
        break;
#ifdef V4L2_CTRL_TYPE_AREA
      case V4L2_CTRL_TYPE_AREA:
        control->values_area_.resize(desc.elems);
        memcpy(control->values_area_.data(), payload.data(),
               kernel_values.size);
        break;
#endif /* V4L2_CTRL_TYPE_AREA */
#ifdef V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS
      case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
        control->values_hdr10_cll_info_.resize(desc.elems);
        memcpy(control->values_hdr10_cll_info_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
        control->values_hdr10_mastering_display_.resize(desc.elems);
        memcpy(control->values_hdr10_mastering_display_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_SPS:
        control->values_h264_sps_.resize(desc.elems);
        memcpy(control->values_h264_sps_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_PPS:
        control->values_h264_pps_.resize(desc.elems);
        memcpy(control->values_h264_pps_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
        control->values_h264_scaling_matrix_.resize(desc.elems);
        memcpy(control->values_h264_scaling_matrix_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_SLICE_PARAMS:
        control->values_h264_slice_params_.resize(desc.elems);
        memcpy(control->values_h264_slice_params_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_DECODE_PARAMS:
        control->values_h264_decode_params_.resize(desc.elems);
        memcpy(control->values_h264_decode_params_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS:
        control->values_h264_pred_weights_.resize(desc.elems);
        memcpy(control->values_h264_pred_weights_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_FWHT_PARAMS:
        control->values_fwht_params_.resize(desc.elems);
        memcpy(control->values_fwht_params_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_VP8_FRAME:
        control->values_vp8_frame_.resize(desc.elems);
        memcpy(control->values_vp8_frame_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
        control->values_mpeg2_quantisation_.resize(desc.elems);
        memcpy(control->values_mpeg2_quantisation_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
        control->values_mpeg2_sequence_.resize(desc.elems);
        memcpy(control->values_mpeg2_sequence_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_MPEG2_PICTURE:
        control->values_mpeg2_picture_.resize(desc.elems);
        memcpy(control->values_mpeg2_picture_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR:
        control->values_vp9_compressed_hdr_.resize(desc.elems);
        memcpy(control->values_vp9_compressed_hdr_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_VP9_FRAME:
        control->values_vp9_frame_.resize(desc.elems);
        memcpy(control->values_vp9_frame_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HEVC_SPS:
        control->values_hevc_sps_.resize(desc.elems);
        memcpy(control->values_hevc_sps_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HEVC_PPS:
        control->values_hevc_pps_.resize(desc.elems);
        memcpy(control->values_hevc_pps_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS:
        control->values_hevc_slice_params_.resize(desc.elems);
        memcpy(control->values_hevc_slice_params_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HEVC_SCALING_MATRIX:
        control->values_hevc_scaling_matrix_.resize(desc.elems);
        memcpy(control->values_hevc_scaling_matrix_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS:
        control->values_hevc_decode_params_.resize(desc.elems);
        memcpy(control->values_hevc_decode_params_.data(), payload.data(),
               kernel_values.size);
        break;
#endif /* V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS */
#ifdef V4L2_CTRL_TYPE_AV1_FILM_GRAIN
      case V4L2_CTRL_TYPE_AV1_SEQUENCE:
        control->values_av1_sequence_.resize(desc.elems);
        memcpy(control->values_av1_sequence_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_AV1_TILE_GROUP_ENTRY:
        control->values_av1_tile_group_entry_.resize(desc.elems);
        memcpy(control->values_av1_tile_group_entry_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_AV1_FRAME:
        control->values_av1_frame_.resize(desc.elems);
        memcpy(control->values_av1_frame_.data(), payload.data(),
               kernel_values.size);
        break;
      case V4L2_CTRL_TYPE_AV1_FILM_GRAIN:
        control->values_av1_film_grain_.resize(desc.elems);
        memcpy(control->values_av1_film_grain_.data(), payload.data(),
               kernel_values.size);
        break;
#endif /* V4L2_CTRL_TYPE_AV1_FILM_GRAIN */
      case V4L2_CTRL_TYPE_CTRL_CLASS:
        /* fall-through */
      default:
        MCTK_PANIC("Unsupported control type encountered");
    }
  }

  /* Clear V4L2_CTRL_FLAG_GRABBED */
  control->desc_.flags &= ~V4L2_CTRL_FLAG_GRABBED;

  return control;
}
