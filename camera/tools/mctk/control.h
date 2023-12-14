/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wrapper class capturing a snapshot of the description and properties of
 * a V4L2 (sub)device's controls.
 *
 * Setter functions primarily update the state in the class.
 * If fd_ent_ is set to an fd to the V4L2 (sub)device, the matching ioctl()s
 * are sent to the kernel, programming the updated values into the driver.
 *
 * If fd_ent_ is set, this class DOES NOT own it and will NOT close it.
 */

#ifndef CAMERA_TOOLS_MCTK_CONTROL_H_
#define CAMERA_TOOLS_MCTK_CONTROL_H_

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/yaml_tree.h"

class V4lMcControl {
 public:
  /* Public functions */

  V4lMcControl() : desc_({}) {}
  explicit V4lMcControl(int fd) : desc_({}), fd_ent_(fd) {}

  /* Factory functions */
  static std::unique_ptr<V4lMcControl> CreateFromKernel(
      struct v4l2_query_ext_ctrl& desc, int fd_ent);
  static std::unique_ptr<V4lMcControl> CreateFromYamlNode(
      YamlNode& node_control);

  bool IsReadOnly();

  /* Setters for controls with value arrays */
  bool Set(std::vector<__s32>& values);
  bool Set(std::vector<__s64>& values);
  bool Set(std::vector<std::string>& values_string);
  bool Set(std::vector<__u8>& values_u8);
  bool Set(std::vector<__u16>& values_u16);
  bool Set(std::vector<__u32>& values_u32);
#ifdef V4L2_CTRL_TYPE_AREA
  bool Set(std::vector<struct v4l2_area>& values_area);
#endif

  /* Setters for single value controls */
  template <typename T>
  bool Set(T value);

  /* Public variables */

  /* Control description, as per VIDIOC_QUERY_EXT_CTRL */
  struct v4l2_query_ext_ctrl desc_;

  /* Control value arrays.
   * Single values are arrays of length 1.
   */
  std::vector<__s32> values_s32_;
  std::vector<__s64> values_s64_;

  std::vector<std::string> values_string_;

  std::vector<__u8> values_u8_;
  std::vector<__u16> values_u16_;
  std::vector<__u32> values_u32_;

#ifdef V4L2_CTRL_TYPE_AREA
  std::vector<struct v4l2_area> values_area_;
#endif /* V4L2_CTRL_TYPE_AREA */

  /* Compound types - currently unsupported */
#ifdef V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS
  std::vector<struct v4l2_ctrl_hdr10_cll_info> values_hdr10_cll_info_;
  std::vector<struct v4l2_ctrl_hdr10_mastering_display>
      values_hdr10_mastering_display_;
  std::vector<struct v4l2_ctrl_h264_sps> values_h264_sps_;
  std::vector<struct v4l2_ctrl_h264_pps> values_h264_pps_;
  std::vector<struct v4l2_ctrl_h264_scaling_matrix> values_h264_scaling_matrix_;
  std::vector<struct v4l2_ctrl_h264_slice_params> values_h264_slice_params_;
  std::vector<struct v4l2_ctrl_h264_decode_params> values_h264_decode_params_;
  std::vector<struct v4l2_ctrl_h264_pred_weights> values_h264_pred_weights_;
  std::vector<struct v4l2_ctrl_fwht_params> values_fwht_params_;
  std::vector<struct v4l2_ctrl_vp8_frame> values_vp8_frame_;
  std::vector<struct v4l2_ctrl_mpeg2_quantisation> values_mpeg2_quantisation_;
  std::vector<struct v4l2_ctrl_mpeg2_sequence> values_mpeg2_sequence_;
  std::vector<struct v4l2_ctrl_mpeg2_picture> values_mpeg2_picture_;
  std::vector<struct v4l2_ctrl_vp9_compressed_hdr> values_vp9_compressed_hdr_;
  std::vector<struct v4l2_ctrl_vp9_frame> values_vp9_frame_;
  std::vector<struct v4l2_ctrl_hevc_sps> values_hevc_sps_;
  std::vector<struct v4l2_ctrl_hevc_pps> values_hevc_pps_;
  std::vector<struct v4l2_ctrl_hevc_slice_params> values_hevc_slice_params_;
  std::vector<struct v4l2_ctrl_hevc_scaling_matrix> values_hevc_scaling_matrix_;
  std::vector<struct v4l2_ctrl_hevc_decode_params> values_hevc_decode_params_;
#endif /* V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS */
#ifdef V4L2_CTRL_TYPE_AV1_FILM_GRAIN
  std::vector<struct v4l2_ctrl_av1_sequence> values_av1_sequence_;
  std::vector<struct v4l2_ctrl_av1_tile_group_entry>
      values_av1_tile_group_entry_;
  std::vector<struct v4l2_ctrl_av1_frame> values_av1_frame_;
  std::vector<struct v4l2_ctrl_av1_film_grain> values_av1_film_grain_;
#endif /* V4L2_CTRL_TYPE_AV1_FILM_GRAIN */

 private:
  /* Private functions */

  bool WrapIoctl(struct v4l2_ext_control& controls_ptr);

  /* Private variables */

  /* Optional fd to V4L2 device containing this control.
   * If this is set, setters will additionally call ioctl() on this fd.
   */
  std::optional<int> fd_ent_;
};

#endif /* CAMERA_TOOLS_MCTK_CONTROL_H_ */
