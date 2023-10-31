/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* YAML serialiser for an abstract model of a V4L2 media controller.
 *
 * NOTE: This is not being reworked because YAML support may be dropped soon.
 */

#include "tools/mctk/mcdev.h"

#include <assert.h>
#include <linux/media.h>
#include <linux/types.h>
#include <stddef.h> /* size_t */
#include <stdio.h>
#include <unistd.h>
#include <yaml.h>

#include <memory>
#include <optional>
#include <vector>

#include "tools/mctk/debug.h"

namespace {

#define EMIT_STREAM_START()                                                 \
  do {                                                                      \
    yaml_event_t event;                                                     \
    assert(yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING)); \
    assert(yaml_emitter_emit(&emitter, &event));                            \
  } while (0);

#define EMIT_STREAM_END()                             \
  do {                                                \
    yaml_event_t event;                               \
    assert(yaml_stream_end_event_initialize(&event)); \
    assert(yaml_emitter_emit(&emitter, &event));      \
  } while (0);

#define EMIT_DOCUMENT_START()                                                  \
  do {                                                                         \
    yaml_event_t event;                                                        \
    assert(yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1)); \
    assert(yaml_emitter_emit(&emitter, &event));                               \
  } while (0);

#define EMIT_DOCUMENT_END()                                \
  do {                                                     \
    yaml_event_t event;                                    \
    assert(yaml_document_end_event_initialize(&event, 1)); \
    assert(yaml_emitter_emit(&emitter, &event));           \
  } while (0);

#define EMIT_MAP_START()                                                     \
  do {                                                                       \
    yaml_event_t event;                                                      \
    assert(yaml_mapping_start_event_initialize(                              \
        &event, NULL, reinterpret_cast<const yaml_char_t*>(YAML_MAP_TAG), 1, \
        YAML_BLOCK_MAPPING_STYLE));                                          \
    assert(yaml_emitter_emit(&emitter, &event));                             \
  } while (0);

#define EMIT_MAP_END()                                 \
  do {                                                 \
    yaml_event_t event;                                \
    assert(yaml_mapping_end_event_initialize(&event)); \
    assert(yaml_emitter_emit(&emitter, &event));       \
  } while (0);

#define EMIT_SEQ_START(sequence_style)                                       \
  do {                                                                       \
    yaml_event_t event;                                                      \
    assert(yaml_sequence_start_event_initialize(                             \
        &event, NULL, reinterpret_cast<const yaml_char_t*>(YAML_SEQ_TAG), 1, \
        (sequence_style)));                                                  \
    assert(yaml_emitter_emit(&emitter, &event));                             \
  } while (0);

#define EMIT_SEQ_END()                                  \
  do {                                                  \
    yaml_event_t event;                                 \
    assert(yaml_sequence_end_event_initialize(&event)); \
    assert(yaml_emitter_emit(&emitter, &event));        \
  } while (0);

#define EMIT_CSTR(value_cstr)                                                 \
  do {                                                                        \
    yaml_event_t event;                                                       \
    assert(yaml_scalar_event_initialize(&event, NULL, NULL,                   \
                                        (const yaml_char_t*)(value_cstr), -1, \
                                        1, 1, YAML_ANY_SCALAR_STYLE));        \
    assert(yaml_emitter_emit(&emitter, &event));                              \
  } while (0);

#define EMIT_KEY_CSTR(value_cstr) \
  do {                            \
    EMIT_CSTR(value_cstr);        \
  } while (0);

#define EMIT_U64(value_u64)                          \
  do {                                               \
    char buf[32];                                    \
    snprintf(buf, sizeof(buf), "%llu", (value_u64)); \
    EMIT_CSTR(buf);                                  \
  } while (0);

#define EMIT_S64(value_s64)                          \
  do {                                               \
    char buf[32];                                    \
    snprintf(buf, sizeof(buf), "%lli", (value_s64)); \
    EMIT_CSTR(buf);                                  \
  } while (0);

#define EMIT_U32(value_u32)                        \
  do {                                             \
    char buf[16];                                  \
    snprintf(buf, sizeof(buf), "%u", (value_u32)); \
    EMIT_CSTR(buf);                                \
  } while (0);

#define EMIT_S32(value_s32)                        \
  do {                                             \
    char buf[16];                                  \
    snprintf(buf, sizeof(buf), "%i", (value_s32)); \
    EMIT_CSTR(buf);                                \
  } while (0);

#define EMIT_U16(value_u16) EMIT_U32(value_u16)

#define EMIT_S16(value_s16) EMIT_S32(value_s16)

#define EMIT_U8(value_u8) EMIT_U32(value_u8)

#define EMIT_S8(value_s8) EMIT_S32(value_s8)

#define EMIT_U64_ELEM(ptr_struct, elem) \
  do {                                  \
    EMIT_KEY_CSTR(#elem);               \
    EMIT_U64((ptr_struct)->elem);       \
  } while (0);

#define EMIT_S64_ELEM(ptr_struct, elem) \
  do {                                  \
    EMIT_KEY_CSTR(#elem);               \
    EMIT_S64((ptr_struct)->elem);       \
  } while (0);

#define EMIT_U32_ELEM(ptr_struct, elem) \
  do {                                  \
    EMIT_KEY_CSTR(#elem);               \
    EMIT_U32((ptr_struct)->elem);       \
  } while (0);

#define EMIT_S32_ELEM(ptr_struct, elem) \
  do {                                  \
    EMIT_KEY_CSTR(#elem);               \
    EMIT_S32((ptr_struct)->elem);       \
  } while (0);

#define EMIT_U16_ELEM(ptr_struct, elem) EMIT_U32_ELEM(ptr_struct, elem)

#define EMIT_S16_ELEM(ptr_struct, elem) EMIT_S32_ELEM(ptr_struct, elem)

#define EMIT_U8_ELEM(ptr_struct, elem) EMIT_U32_ELEM(ptr_struct, elem)

#define EMIT_S8_ELEM(ptr_struct, elem) EMIT_S32_ELEM(ptr_struct, elem)

#define EMIT_U64_ELEM_ARRAY(ptr_struct, elem, count) \
  do {                                               \
    EMIT_KEY_CSTR(#elem);                            \
    EMIT_SEQ_START(YAML_FLOW_SEQUENCE_STYLE);        \
    for (size_t i = 0; i < count; i++) {             \
      EMIT_U64((ptr_struct)->elem[i]);               \
    }                                                \
    EMIT_SEQ_END();                                  \
  } while (0);

#define EMIT_U32_ELEM_ARRAY(ptr_struct, elem, count) \
  do {                                               \
    EMIT_KEY_CSTR(#elem);                            \
    EMIT_SEQ_START(YAML_FLOW_SEQUENCE_STYLE);        \
    for (size_t i = 0; i < count; i++) {             \
      EMIT_U32((ptr_struct)->elem[i]);               \
    }                                                \
    EMIT_SEQ_END();                                  \
  } while (0);

#define EMIT_S32_ELEM_ARRAY(ptr_struct, elem, count) \
  do {                                               \
    EMIT_KEY_CSTR(#elem);                            \
    EMIT_SEQ_START(YAML_FLOW_SEQUENCE_STYLE);        \
    for (size_t i = 0; i < count; i++) {             \
      EMIT_S32((ptr_struct)->elem[i]);               \
    }                                                \
    EMIT_SEQ_END();                                  \
  } while (0);

#define EMIT_U16_ELEM_ARRAY(ptr_struct, elem, count) \
  EMIT_U32_ELEM_ARRAY(ptr_struct, elem, count)

#define EMIT_S16_ELEM_ARRAY(ptr_struct, elem, count) \
  EMIT_S32_ELEM_ARRAY(ptr_struct, elem, count)

#define EMIT_U8_ELEM_ARRAY(ptr_struct, elem, count) \
  EMIT_U32_ELEM_ARRAY(ptr_struct, elem, count)

#define EMIT_S8_ELEM_ARRAY(ptr_struct, elem, count) \
  EMIT_S32_ELEM_ARRAY(ptr_struct, elem, count)

#define EMIT_FRACT(ptr_fract)                \
  do {                                       \
    EMIT_MAP_START();                        \
    EMIT_U32_ELEM((ptr_fract), numerator);   \
    EMIT_U32_ELEM((ptr_fract), denominator); \
    EMIT_MAP_END();                          \
  } while (0);

#define EMIT_RECT(ptr_rect)            \
  do {                                 \
    EMIT_MAP_START();                  \
    EMIT_S32_ELEM((ptr_rect), left);   \
    EMIT_S32_ELEM((ptr_rect), top);    \
    EMIT_U32_ELEM((ptr_rect), width);  \
    EMIT_U32_ELEM((ptr_rect), height); \
    EMIT_MAP_END();                    \
  } while (0);

#define EMIT_SELECTION(ptr_selection)                    \
  do {                                                   \
    EMIT_MAP_START();                                    \
    if ((ptr_selection)->crop_.has_value()) {            \
      EMIT_KEY_CSTR("crop");                             \
      EMIT_RECT(&*((ptr_selection)->crop_));             \
    }                                                    \
    if ((ptr_selection)->crop_default_.has_value()) {    \
      EMIT_KEY_CSTR("crop_default");                     \
      EMIT_RECT(&*((ptr_selection)->crop_default_));     \
    }                                                    \
    if ((ptr_selection)->crop_bounds_.has_value()) {     \
      EMIT_KEY_CSTR("crop_bounds");                      \
      EMIT_RECT(&*((ptr_selection)->crop_bounds_));      \
    }                                                    \
    if ((ptr_selection)->native_size_.has_value()) {     \
      EMIT_KEY_CSTR("native_size");                      \
      EMIT_RECT(&*((ptr_selection)->native_size_));      \
    }                                                    \
                                                         \
    if ((ptr_selection)->compose_.has_value()) {         \
      EMIT_KEY_CSTR("compose");                          \
      EMIT_RECT(&*((ptr_selection)->compose_));          \
    }                                                    \
    if ((ptr_selection)->compose_default_.has_value()) { \
      EMIT_KEY_CSTR("compose_default");                  \
      EMIT_RECT(&*((ptr_selection)->compose_default_));  \
    }                                                    \
    if ((ptr_selection)->compose_bounds_.has_value()) {  \
      EMIT_KEY_CSTR("compose_bounds");                   \
      EMIT_RECT(&*((ptr_selection)->compose_bounds_));   \
    }                                                    \
    if ((ptr_selection)->compose_padded_.has_value()) {  \
      EMIT_KEY_CSTR("compose_padded");                   \
      EMIT_RECT(&*((ptr_selection)->compose_padded_));   \
    }                                                    \
    EMIT_MAP_END();                                      \
  } while (0);

#define EMIT_DV_TIMINGS(ptr_dv_timings)                       \
  do {                                                        \
    EMIT_MAP_START();                                         \
    EMIT_U32_ELEM((ptr_dv_timings), type);                    \
    EMIT_KEY_CSTR("bt");                                      \
    EMIT_MAP_START();                                         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, width);              \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, height);             \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, interlaced);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, polarities);         \
    EMIT_U64_ELEM(&(ptr_dv_timings)->bt, pixelclock);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, hfrontporch);        \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, hsync);              \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, hbackporch);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, vfrontporch);        \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, vsync);              \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, vbackporch);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, il_vfrontporch);     \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, il_vsync);           \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, il_vbackporch);      \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, standards);          \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, flags);              \
    EMIT_KEY_CSTR("picture_aspect");                          \
    EMIT_FRACT(&(ptr_dv_timings)->bt.picture_aspect);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, cea861_vic);         \
    EMIT_U32_ELEM(&(ptr_dv_timings)->bt, hdmi_vic);           \
    EMIT_U32_ELEM_ARRAY(&(ptr_dv_timings)->bt, reserved, 46); \
    EMIT_MAP_END();                                           \
    EMIT_MAP_END();                                           \
  } while (0);

#define EMIT_CAPTUREPARM(ptr_captureparm)                \
  do {                                                   \
    EMIT_MAP_START();                                    \
    EMIT_U32_ELEM((ptr_captureparm), capability);        \
    EMIT_U32_ELEM((ptr_captureparm), capturemode);       \
    EMIT_KEY_CSTR("timeperframe");                       \
    EMIT_FRACT(&((ptr_captureparm)->timeperframe));      \
    EMIT_U32_ELEM((ptr_captureparm), extendedmode);      \
    EMIT_U32_ELEM((ptr_captureparm), readbuffers);       \
    EMIT_U32_ELEM_ARRAY((ptr_captureparm), reserved, 4); \
    EMIT_MAP_END();                                      \
  } while (0);

#define EMIT_OUTPUTPARM(ptr_outputparm)                 \
  do {                                                  \
    EMIT_MAP_START();                                   \
    EMIT_U32_ELEM((ptr_outputparm), capability);        \
    EMIT_U32_ELEM((ptr_outputparm), outputmode);        \
    EMIT_KEY_CSTR("timeperframe");                      \
    EMIT_FRACT(&(ptr_outputparm)->timeperframe);        \
    EMIT_U32_ELEM((ptr_outputparm), extendedmode);      \
    EMIT_U32_ELEM((ptr_outputparm), writebuffers);      \
    EMIT_U32_ELEM_ARRAY((ptr_outputparm), reserved, 4); \
    EMIT_MAP_END();                                     \
  } while (0);

void EmitControlValues(yaml_emitter_t& emitter, V4lMcControl& control) {
  switch (control.desc_.type) {
    case V4L2_CTRL_TYPE_INTEGER:
      /* fall-through */
    case V4L2_CTRL_TYPE_BOOLEAN:
      /* fall-through */
    case V4L2_CTRL_TYPE_MENU:
      /* fall-through */
    case V4L2_CTRL_TYPE_BUTTON:
    case V4L2_CTRL_TYPE_BITMASK:
    case V4L2_CTRL_TYPE_INTEGER_MENU: {
      for (__s32 tmp : control.values_s32_)
        EMIT_S32(tmp);
      break;
    }
    case V4L2_CTRL_TYPE_INTEGER64: {
      for (__s64 tmp : control.values_s64_)
        EMIT_S64(tmp);
      break;
    }
    case V4L2_CTRL_TYPE_CTRL_CLASS:
      /* This should never happen:
       * We enumerate controls, not control classes.
       */
      assert(0);
      break;
    case V4L2_CTRL_TYPE_STRING:
      for (std::string str : control.values_string_)
        EMIT_CSTR(str.c_str());
      break;
    case V4L2_CTRL_TYPE_U8: {
      for (__u8 tmp : control.values_u8_)
        EMIT_U8(tmp);
      break;
    }
    case V4L2_CTRL_TYPE_U16: {
      for (__u16 tmp : control.values_u16_)
        EMIT_U16(tmp);
      break;
    }
    case V4L2_CTRL_TYPE_U32: {
      for (__u32 tmp : control.values_u32_)
        EMIT_U32(tmp);
      break;
    }
#ifdef V4L2_CTRL_TYPE_AREA
    case V4L2_CTRL_TYPE_AREA: {
      for (struct v4l2_area tmp : control.values_area_) {
        EMIT_MAP_START();
        EMIT_U32_ELEM(&tmp, width);
        EMIT_U32_ELEM(&tmp, height);
        EMIT_MAP_END();
      }
      break;
    }
#endif /* V4L2_CTRL_TYPE_AREA */
#ifdef V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS
    case V4L2_CTRL_TYPE_HDR10_CLL_INFO: {
      for (struct v4l2_ctrl_hdr10_cll_info tmp :
           control.values_hdr10_cll_info_) {
        EMIT_MAP_START();
        EMIT_U16_ELEM(&tmp, max_content_light_level);
        EMIT_U16_ELEM(&tmp, max_pic_average_light_level);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY: {
      for (struct v4l2_ctrl_hdr10_mastering_display tmp :
           control.values_hdr10_mastering_display_) {
        EMIT_MAP_START();
        EMIT_U16_ELEM_ARRAY(&tmp, display_primaries_x, 3);
        EMIT_U16_ELEM_ARRAY(&tmp, display_primaries_y, 3);
        EMIT_U16_ELEM(&tmp, white_point_x);
        EMIT_U16_ELEM(&tmp, white_point_y);
        EMIT_U32_ELEM(&tmp, max_display_mastering_luminance);
        EMIT_U32_ELEM(&tmp, min_display_mastering_luminance);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_SPS: {
      for (struct v4l2_ctrl_h264_sps tmp : control.values_h264_sps_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, profile_idc);
        EMIT_U8_ELEM(&tmp, constraint_set_flags);
        EMIT_U8_ELEM(&tmp, level_idc);
        EMIT_U8_ELEM(&tmp, seq_parameter_set_id);
        EMIT_U8_ELEM(&tmp, chroma_format_idc);
        EMIT_U8_ELEM(&tmp, bit_depth_luma_minus8);
        EMIT_U8_ELEM(&tmp, bit_depth_chroma_minus8);
        EMIT_U8_ELEM(&tmp, log2_max_frame_num_minus4);
        EMIT_U8_ELEM(&tmp, pic_order_cnt_type);
        EMIT_U8_ELEM(&tmp, log2_max_pic_order_cnt_lsb_minus4);
        EMIT_U8_ELEM(&tmp, max_num_ref_frames);
        EMIT_U8_ELEM(&tmp, num_ref_frames_in_pic_order_cnt_cycle);
        EMIT_S32_ELEM_ARRAY(&tmp, offset_for_ref_frame, 255);
        EMIT_S32_ELEM(&tmp, offset_for_non_ref_pic);
        EMIT_S32_ELEM(&tmp, offset_for_top_to_bottom_field);
        EMIT_U16_ELEM(&tmp, pic_width_in_mbs_minus1);
        EMIT_U16_ELEM(&tmp, pic_height_in_map_units_minus1);
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_PPS: {
      for (struct v4l2_ctrl_h264_pps tmp : control.values_h264_pps_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, pic_parameter_set_id);
        EMIT_U8_ELEM(&tmp, seq_parameter_set_id);
        EMIT_U8_ELEM(&tmp, num_slice_groups_minus1);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l0_default_active_minus1);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l1_default_active_minus1);
        EMIT_U8_ELEM(&tmp, weighted_bipred_idc);
        EMIT_S8_ELEM(&tmp, pic_init_qp_minus26);
        EMIT_S8_ELEM(&tmp, pic_init_qs_minus26);
        EMIT_S8_ELEM(&tmp, chroma_qp_index_offset);
        EMIT_S8_ELEM(&tmp, second_chroma_qp_index_offset);
        EMIT_U16_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_SCALING_MATRIX: {
      for (struct v4l2_ctrl_h264_scaling_matrix tmp :
           control.values_h264_scaling_matrix_) {
        EMIT_MAP_START();
        // TODO(b/297144798): scaling_list_4x4
        // TODO(b/297144798): scaling_list_8x8
        (void)tmp;
        assert(0);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_SLICE_PARAMS: {
      for (struct v4l2_ctrl_h264_slice_params tmp :
           control.values_h264_slice_params_) {
        EMIT_MAP_START();
        EMIT_U32_ELEM(&tmp, header_bit_size);
        EMIT_U32_ELEM(&tmp, first_mb_in_slice);
        EMIT_U8_ELEM(&tmp, slice_type);
        EMIT_U8_ELEM(&tmp, colour_plane_id);
        EMIT_U8_ELEM(&tmp, redundant_pic_cnt);
        EMIT_U8_ELEM(&tmp, cabac_init_idc);
        EMIT_S8_ELEM(&tmp, slice_qp_delta);
        EMIT_S8_ELEM(&tmp, slice_qs_delta);
        EMIT_U8_ELEM(&tmp, disable_deblocking_filter_idc);
        EMIT_S8_ELEM(&tmp, slice_alpha_c0_offset_div2);
        EMIT_S8_ELEM(&tmp, slice_beta_offset_div2);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l0_active_minus1);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l1_active_minus1);

        EMIT_U8_ELEM(&tmp, reserved);

        // TODO(b/297144798): ref_pic_list0
        // TODO(b/297144798): ref_pic_list1
        assert(0);

        EMIT_U32_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_DECODE_PARAMS: {
      for (struct v4l2_ctrl_h264_decode_params tmp :
           control.values_h264_decode_params_) {
        EMIT_MAP_START();
        // TODO(b/297144798): v4l2_h264_dpb_entry
        assert(0);

        EMIT_U16_ELEM(&tmp, nal_ref_idc);
        EMIT_U16_ELEM(&tmp, frame_num);
        EMIT_S32_ELEM(&tmp, top_field_order_cnt);
        EMIT_S32_ELEM(&tmp, bottom_field_order_cnt);
        EMIT_U16_ELEM(&tmp, idr_pic_id);
        EMIT_U16_ELEM(&tmp, pic_order_cnt_lsb);
        EMIT_S32_ELEM(&tmp, delta_pic_order_cnt_bottom);
        EMIT_S32_ELEM(&tmp, delta_pic_order_cnt0);
        EMIT_S32_ELEM(&tmp, delta_pic_order_cnt1);
        EMIT_U32_ELEM(&tmp, dec_ref_pic_marking_bit_size);
        EMIT_U32_ELEM(&tmp, pic_order_cnt_bit_size);
        EMIT_U32_ELEM(&tmp, slice_group_change_cycle);

        EMIT_U32_ELEM(&tmp, reserved);
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS: {
      for (struct v4l2_ctrl_h264_pred_weights tmp :
           control.values_h264_pred_weights_) {
        EMIT_MAP_START();
        EMIT_U16_ELEM(&tmp, luma_log2_weight_denom);
        EMIT_U16_ELEM(&tmp, chroma_log2_weight_denom);

        // TODO(b/297144798): v4l2_h264_dpb_entry
        assert(0);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_FWHT_PARAMS: {
      for (struct v4l2_ctrl_fwht_params tmp : control.values_fwht_params_) {
        EMIT_MAP_START();
        EMIT_U64_ELEM(&tmp, backward_ref_ts);
        EMIT_U32_ELEM(&tmp, version);
        EMIT_U32_ELEM(&tmp, width);
        EMIT_U32_ELEM(&tmp, height);
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_U32_ELEM(&tmp, colorspace);
        EMIT_U32_ELEM(&tmp, xfer_func);
        EMIT_U32_ELEM(&tmp, ycbcr_enc);
        EMIT_U32_ELEM(&tmp, quantization);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_VP8_FRAME: {
      for (struct v4l2_ctrl_vp8_frame tmp : control.values_vp8_frame_) {
        EMIT_MAP_START();
        // TODO(b/297144798): segment
        // TODO(b/297144798): lf
        // TODO(b/297144798): quant
        // TODO(b/297144798): entropy
        // TODO(b/297144798): coder_state
        assert(0);

        EMIT_U16_ELEM(&tmp, width);
        EMIT_U16_ELEM(&tmp, height);

        EMIT_U8_ELEM(&tmp, horizontal_scale);
        EMIT_U8_ELEM(&tmp, vertical_scale);

        EMIT_U8_ELEM(&tmp, version);
        EMIT_U8_ELEM(&tmp, prob_skip_false);
        EMIT_U8_ELEM(&tmp, prob_intra);
        EMIT_U8_ELEM(&tmp, prob_last);
        EMIT_U8_ELEM(&tmp, prob_gf);
        EMIT_U8_ELEM(&tmp, num_dct_parts);

        EMIT_U32_ELEM(&tmp, first_part_size);
        EMIT_U32_ELEM(&tmp, first_part_header_bits);
        EMIT_U32_ELEM_ARRAY(&tmp, dct_part_sizes, 8);

        EMIT_U64_ELEM(&tmp, last_frame_ts);
        EMIT_U64_ELEM(&tmp, golden_frame_ts);
        EMIT_U64_ELEM(&tmp, alt_frame_ts);

        EMIT_U64_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_MPEG2_QUANTISATION: {
      for (struct v4l2_ctrl_mpeg2_quantisation tmp :
           control.values_mpeg2_quantisation_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM_ARRAY(&tmp, intra_quantiser_matrix, 64);
        EMIT_U8_ELEM_ARRAY(&tmp, non_intra_quantiser_matrix, 64);
        EMIT_U8_ELEM_ARRAY(&tmp, chroma_intra_quantiser_matrix, 64);
        EMIT_U8_ELEM_ARRAY(&tmp, chroma_non_intra_quantiser_matrix, 64);
        // TODO(b/297144798): segment
        // TODO(b/297144798): lf
        // TODO(b/297144798): quant
        // TODO(b/297144798): entropy
        // TODO(b/297144798): coder_state
        assert(0);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_MPEG2_SEQUENCE: {
      for (struct v4l2_ctrl_mpeg2_sequence tmp :
           control.values_mpeg2_sequence_) {
        EMIT_MAP_START();
        EMIT_U16_ELEM(&tmp, horizontal_size);
        EMIT_U16_ELEM(&tmp, vertical_size);
        EMIT_U32_ELEM(&tmp, vbv_buffer_size);
        EMIT_U16_ELEM(&tmp, profile_and_level_indication);
        EMIT_U8_ELEM(&tmp, chroma_format);
        EMIT_U8_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_MPEG2_PICTURE: {
      for (struct v4l2_ctrl_mpeg2_picture tmp : control.values_mpeg2_picture_) {
        EMIT_MAP_START();
        EMIT_U64_ELEM(&tmp, backward_ref_ts);
        EMIT_U64_ELEM(&tmp, forward_ref_ts);
        EMIT_U32_ELEM(&tmp, flags);
        // TODO(b/297144798): __u8 f_code[2][2];
        assert(0);
        EMIT_U8_ELEM(&tmp, picture_coding_type);
        EMIT_U8_ELEM(&tmp, picture_structure);
        EMIT_U8_ELEM(&tmp, intra_dc_precision);
        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 5);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR: {
      for (struct v4l2_ctrl_vp9_compressed_hdr tmp :
           control.values_vp9_compressed_hdr_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, tx_mode);
        // TODO(b/297144798): __u8 tx8[2][1];
        // TODO(b/297144798): __u8 tx16[2][2];
        // TODO(b/297144798): __u8 tx32[2][3];
        // TODO(b/297144798): __u8 coef[4][2][2][6][6][3];
        // TODO(b/297144798): __u8 skip[3];
        // TODO(b/297144798): __u8 inter_mode[7][3];
        // TODO(b/297144798): __u8 interp_filter[4][2];
        // TODO(b/297144798): __u8 is_inter[4];
        // TODO(b/297144798): __u8 comp_mode[5];
        // TODO(b/297144798): __u8 single_ref[5][2];
        // TODO(b/297144798): __u8 comp_ref[5];
        // TODO(b/297144798): __u8 y_mode[4][9];
        // TODO(b/297144798): __u8 uv_mode[10][9];
        // TODO(b/297144798): __u8 partition[16][3];

        // TODO(b/297144798): struct v4l2_vp9_mv_probs mv;
        assert(0);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_VP9_FRAME: {
      for (struct v4l2_ctrl_vp9_frame tmp : control.values_vp9_frame_) {
        EMIT_MAP_START();
        // TODO(b/297144798): struct v4l2_vp9_loop_filter lf;
        // TODO(b/297144798): struct v4l2_vp9_quantization quant;
        // TODO(b/297144798): struct v4l2_vp9_segmentation seg;
        assert(0);
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_U16_ELEM(&tmp, compressed_header_size);
        EMIT_U16_ELEM(&tmp, uncompressed_header_size);
        EMIT_U16_ELEM(&tmp, frame_width_minus_1);
        EMIT_U16_ELEM(&tmp, frame_height_minus_1);
        EMIT_U16_ELEM(&tmp, render_width_minus_1);
        EMIT_U16_ELEM(&tmp, render_height_minus_1);
        EMIT_U64_ELEM(&tmp, last_frame_ts);
        EMIT_U64_ELEM(&tmp, golden_frame_ts);
        EMIT_U64_ELEM(&tmp, alt_frame_ts);
        EMIT_U8_ELEM(&tmp, ref_frame_sign_bias);
        EMIT_U8_ELEM(&tmp, reset_frame_context);
        EMIT_U8_ELEM(&tmp, frame_context_idx);
        EMIT_U8_ELEM(&tmp, profile);
        EMIT_U8_ELEM(&tmp, bit_depth);
        EMIT_U8_ELEM(&tmp, interpolation_filter);
        EMIT_U8_ELEM(&tmp, tile_cols_log2);
        EMIT_U8_ELEM(&tmp, tile_rows_log2);
        EMIT_U8_ELEM(&tmp, reference_mode);
        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 7);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HEVC_SPS: {
      for (struct v4l2_ctrl_hevc_sps tmp : control.values_hevc_sps_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, video_parameter_set_id);
        EMIT_U8_ELEM(&tmp, seq_parameter_set_id);
        EMIT_U16_ELEM(&tmp, pic_width_in_luma_samples);
        EMIT_U16_ELEM(&tmp, pic_height_in_luma_samples);
        EMIT_U8_ELEM(&tmp, bit_depth_luma_minus8);
        EMIT_U8_ELEM(&tmp, bit_depth_chroma_minus8);
        EMIT_U8_ELEM(&tmp, log2_max_pic_order_cnt_lsb_minus4);
        EMIT_U8_ELEM(&tmp, sps_max_dec_pic_buffering_minus1);
        EMIT_U8_ELEM(&tmp, sps_max_num_reorder_pics);
        EMIT_U8_ELEM(&tmp, sps_max_latency_increase_plus1);
        EMIT_U8_ELEM(&tmp, log2_min_luma_coding_block_size_minus3);
        EMIT_U8_ELEM(&tmp, log2_diff_max_min_luma_coding_block_size);
        EMIT_U8_ELEM(&tmp, log2_min_luma_transform_block_size_minus2);
        EMIT_U8_ELEM(&tmp, log2_diff_max_min_luma_transform_block_size);
        EMIT_U8_ELEM(&tmp, max_transform_hierarchy_depth_inter);
        EMIT_U8_ELEM(&tmp, max_transform_hierarchy_depth_intra);
        EMIT_U8_ELEM(&tmp, pcm_sample_bit_depth_luma_minus1);
        EMIT_U8_ELEM(&tmp, pcm_sample_bit_depth_chroma_minus1);
        EMIT_U8_ELEM(&tmp, log2_min_pcm_luma_coding_block_size_minus3);
        EMIT_U8_ELEM(&tmp, log2_diff_max_min_pcm_luma_coding_block_size);
        EMIT_U8_ELEM(&tmp, num_short_term_ref_pic_sets);
        EMIT_U8_ELEM(&tmp, num_long_term_ref_pics_sps);
        EMIT_U8_ELEM(&tmp, chroma_format_idc);
        EMIT_U8_ELEM(&tmp, sps_max_sub_layers_minus1);

        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 6);
        EMIT_U64_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HEVC_PPS: {
      for (struct v4l2_ctrl_hevc_pps tmp : control.values_hevc_pps_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, pic_parameter_set_id);
        EMIT_U8_ELEM(&tmp, num_extra_slice_header_bits);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l0_default_active_minus1);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l1_default_active_minus1);
        EMIT_S8_ELEM(&tmp, init_qp_minus26);
        EMIT_U8_ELEM(&tmp, diff_cu_qp_delta_depth);
        EMIT_S8_ELEM(&tmp, pps_cb_qp_offset);
        EMIT_S8_ELEM(&tmp, pps_cr_qp_offset);
        EMIT_U8_ELEM(&tmp, num_tile_columns_minus1);
        EMIT_U8_ELEM(&tmp, num_tile_rows_minus1);
        EMIT_U8_ELEM_ARRAY(&tmp, column_width_minus1, 20);
        EMIT_U8_ELEM_ARRAY(&tmp, row_height_minus1, 22);
        EMIT_S8_ELEM(&tmp, pps_beta_offset_div2);
        EMIT_S8_ELEM(&tmp, pps_tc_offset_div2);
        EMIT_U8_ELEM(&tmp, log2_parallel_merge_level_minus2);
        EMIT_U8_ELEM(&tmp, reserved);
        EMIT_U64_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS: {
      for (struct v4l2_ctrl_hevc_slice_params tmp :
           control.values_hevc_slice_params_) {
        EMIT_MAP_START();
        EMIT_U32_ELEM(&tmp, bit_size);
        EMIT_U32_ELEM(&tmp, data_byte_offset);
        EMIT_U32_ELEM(&tmp, num_entry_point_offsets);

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: NAL unit header */
        EMIT_U8_ELEM(&tmp, nal_unit_type);
        EMIT_U8_ELEM(&tmp, nuh_temporal_id_plus1);

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        EMIT_U8_ELEM(&tmp, slice_type);
        EMIT_U8_ELEM(&tmp, colour_plane_id);
        EMIT_S32_ELEM(&tmp, slice_pic_order_cnt);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l0_active_minus1);
        EMIT_U8_ELEM(&tmp, num_ref_idx_l1_active_minus1);
        EMIT_U8_ELEM(&tmp, collocated_ref_idx);
        EMIT_U8_ELEM(&tmp, five_minus_max_num_merge_cand);
        EMIT_S8_ELEM(&tmp, slice_qp_delta);
        EMIT_S8_ELEM(&tmp, slice_cb_qp_offset);
        EMIT_S8_ELEM(&tmp, slice_cr_qp_offset);
        EMIT_S8_ELEM(&tmp, slice_act_y_qp_offset);
        EMIT_S8_ELEM(&tmp, slice_act_cb_qp_offset);
        EMIT_S8_ELEM(&tmp, slice_act_cr_qp_offset);
        EMIT_S8_ELEM(&tmp, slice_beta_offset_div2);
        EMIT_S8_ELEM(&tmp, slice_tc_offset_div2);

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture timing SEI message */
        EMIT_U8_ELEM(&tmp, pic_struct);

        EMIT_U8_ELEM_ARRAY(&tmp, reserved0, 3);
        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        EMIT_U32_ELEM(&tmp, slice_segment_addr);
        EMIT_U8_ELEM_ARRAY(&tmp, ref_idx_l0, V4L2_HEVC_DPB_ENTRIES_NUM_MAX);
        EMIT_U8_ELEM_ARRAY(&tmp, ref_idx_l1, V4L2_HEVC_DPB_ENTRIES_NUM_MAX);
        EMIT_U16_ELEM(&tmp, short_term_ref_pic_set_size);
        EMIT_U16_ELEM(&tmp, long_term_ref_pic_set_size);

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: Weighted prediction parameter */
        // TODO(b/297144798): struct v4l2_hevc_pred_weight_table
        //                                                  pred_weight_table;
        assert(0);

        EMIT_U8_ELEM_ARRAY(&tmp, reserved1, 2);
        EMIT_U64_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HEVC_SCALING_MATRIX: {
      for (struct v4l2_ctrl_hevc_scaling_matrix tmp :
           control.values_hevc_scaling_matrix_) {
        EMIT_MAP_START();
        // TODO(b/297144798): __u8 scaling_list_4x4[6][16];
        // TODO(b/297144798): __u8 scaling_list_8x8[6][64];
        // TODO(b/297144798): __u8 scaling_list_16x16[6][64];
        // TODO(b/297144798): __u8 scaling_list_32x32[2][64];
        assert(0);
        EMIT_U8_ELEM_ARRAY(&tmp, scaling_list_dc_coef_16x16, 6);
        EMIT_U8_ELEM_ARRAY(&tmp, scaling_list_dc_coef_32x32, 2);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS: {
      for (struct v4l2_ctrl_hevc_decode_params tmp :
           control.values_hevc_decode_params_) {
        EMIT_MAP_START();
        EMIT_S32_ELEM(&tmp, pic_order_cnt_val);
        EMIT_U16_ELEM(&tmp, short_term_ref_pic_set_size);
        EMIT_U16_ELEM(&tmp, long_term_ref_pic_set_size);
        EMIT_U8_ELEM(&tmp, num_active_dpb_entries);
        EMIT_U8_ELEM(&tmp, num_poc_st_curr_before);
        EMIT_U8_ELEM(&tmp, num_poc_st_curr_after);
        EMIT_U8_ELEM(&tmp, num_poc_lt_curr);
        EMIT_U8_ELEM_ARRAY(&tmp, poc_st_curr_before,
                           V4L2_HEVC_DPB_ENTRIES_NUM_MAX);
        EMIT_U8_ELEM_ARRAY(&tmp, poc_st_curr_after,
                           V4L2_HEVC_DPB_ENTRIES_NUM_MAX);
        EMIT_U8_ELEM_ARRAY(&tmp, poc_lt_curr, V4L2_HEVC_DPB_ENTRIES_NUM_MAX);
        EMIT_U8_ELEM(&tmp, num_delta_pocs_of_ref_rps_idx);
        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 3);
        // TODO(b/297144798): struct v4l2_hevc_dpb_entry
        //                                dpb[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
        assert(0);
        EMIT_U64_ELEM(&tmp, flags);
        EMIT_MAP_END();
      }
      break;
    }
#endif /* V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS */
#ifdef V4L2_CTRL_TYPE_AV1_FILM_GRAIN
    case V4L2_CTRL_TYPE_AV1_SEQUENCE: {
      for (struct v4l2_ctrl_av1_sequence tmp : control.values_av1_sequence_) {
        EMIT_MAP_START();
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_U8_ELEM(&tmp, seq_profile);
        EMIT_U8_ELEM(&tmp, order_hint_bits);
        EMIT_U8_ELEM(&tmp, bit_depth);
        EMIT_U8_ELEM(&tmp, reserved);
        EMIT_U16_ELEM(&tmp, max_frame_width_minus_1);
        EMIT_U16_ELEM(&tmp, max_frame_height_minus_1);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_AV1_TILE_GROUP_ENTRY: {
      for (struct v4l2_ctrl_av1_tile_group_entry tmp :
           control.values_av1_tile_group_entry_) {
        EMIT_MAP_START();
        EMIT_U32_ELEM(&tmp, tile_offset);
        EMIT_U32_ELEM(&tmp, tile_size);
        EMIT_U32_ELEM(&tmp, tile_row);
        EMIT_U32_ELEM(&tmp, tile_col);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_AV1_FRAME: {
      for (struct v4l2_ctrl_av1_frame tmp : control.values_av1_frame_) {
        EMIT_MAP_START();
        // TODO(b/297144798): struct v4l2_av1_tile_info tile_info;
        // TODO(b/297144798): struct v4l2_av1_quantization quantization;
        EMIT_U8_ELEM(&tmp, superres_denom);
        // TODO(b/297144798): struct v4l2_av1_segmentation segmentation;
        // TODO(b/297144798): struct v4l2_av1_loop_filter loop_filter;
        // TODO(b/297144798): struct v4l2_av1_cdef cdef;
        EMIT_U8_ELEM_ARRAY(&tmp, skip_mode_frame, 2);
        EMIT_U8_ELEM(&tmp, primary_ref_frame);
        // TODO(b/297144798): struct v4l2_av1_loop_restoration loop_restoration;
        // TODO(b/297144798): struct v4l2_av1_global_motion global_motion;
        assert(0);
        EMIT_U32_ELEM(&tmp, flags);
        EMIT_U32_ELEM(&tmp, frame_type);
        EMIT_U32_ELEM(&tmp, order_hint);
        EMIT_U32_ELEM(&tmp, upscaled_width);
        EMIT_U32_ELEM(&tmp, interpolation_filter);
        EMIT_U32_ELEM(&tmp, tx_mode);
        EMIT_U32_ELEM(&tmp, frame_width_minus_1);
        EMIT_U32_ELEM(&tmp, frame_height_minus_1);
        EMIT_U16_ELEM(&tmp, render_width_minus_1);
        EMIT_U16_ELEM(&tmp, render_height_minus_1);

        EMIT_U32_ELEM(&tmp, current_frame_id);
        EMIT_U32_ELEM_ARRAY(&tmp, buffer_removal_time,
                            V4L2_AV1_MAX_OPERATING_POINTS);
        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 4);
        EMIT_U32_ELEM_ARRAY(&tmp, order_hints, V4L2_AV1_TOTAL_REFS_PER_FRAME);
        EMIT_U64_ELEM_ARRAY(&tmp, reference_frame_ts,
                            V4L2_AV1_TOTAL_REFS_PER_FRAME);
        EMIT_S8_ELEM_ARRAY(&tmp, ref_frame_idx, V4L2_AV1_REFS_PER_FRAME);
        EMIT_U8_ELEM(&tmp, refresh_frame_flags);
        EMIT_MAP_END();
      }
      break;
    }
    case V4L2_CTRL_TYPE_AV1_FILM_GRAIN: {
      for (struct v4l2_ctrl_av1_film_grain tmp :
           control.values_av1_film_grain_) {
        EMIT_MAP_START();
        EMIT_U8_ELEM(&tmp, flags);
        EMIT_U8_ELEM(&tmp, cr_mult);
        EMIT_U16_ELEM(&tmp, grain_seed);
        EMIT_U8_ELEM(&tmp, film_grain_params_ref_idx);
        EMIT_U8_ELEM(&tmp, num_y_points);
        EMIT_U8_ELEM_ARRAY(&tmp, point_y_value, V4L2_AV1_MAX_NUM_Y_POINTS);
        EMIT_U8_ELEM_ARRAY(&tmp, point_y_scaling, V4L2_AV1_MAX_NUM_Y_POINTS);
        EMIT_U8_ELEM(&tmp, num_cb_points);
        EMIT_U8_ELEM_ARRAY(&tmp, point_cb_value, V4L2_AV1_MAX_NUM_CB_POINTS);
        EMIT_U8_ELEM_ARRAY(&tmp, point_cb_scaling, V4L2_AV1_MAX_NUM_CB_POINTS);
        EMIT_U8_ELEM(&tmp, num_cr_points);
        EMIT_U8_ELEM_ARRAY(&tmp, point_cr_value, V4L2_AV1_MAX_NUM_CR_POINTS);
        EMIT_U8_ELEM_ARRAY(&tmp, point_cr_scaling, V4L2_AV1_MAX_NUM_CR_POINTS);
        EMIT_U8_ELEM(&tmp, grain_scaling_minus_8);
        EMIT_U8_ELEM(&tmp, ar_coeff_lag);
        EMIT_U8_ELEM_ARRAY(&tmp, ar_coeffs_y_plus_128, V4L2_AV1_AR_COEFFS_SIZE);
        EMIT_U8_ELEM_ARRAY(&tmp, ar_coeffs_cb_plus_128,
                           V4L2_AV1_AR_COEFFS_SIZE);
        EMIT_U8_ELEM_ARRAY(&tmp, ar_coeffs_cr_plus_128,
                           V4L2_AV1_AR_COEFFS_SIZE);
        EMIT_U8_ELEM(&tmp, ar_coeff_shift_minus_6);
        EMIT_U8_ELEM(&tmp, grain_scale_shift);
        EMIT_U8_ELEM(&tmp, cb_mult);
        EMIT_U8_ELEM(&tmp, cb_luma_mult);
        EMIT_U8_ELEM(&tmp, cr_luma_mult);
        EMIT_U16_ELEM(&tmp, cb_offset);
        EMIT_U16_ELEM(&tmp, cr_offset);
        EMIT_U8_ELEM_ARRAY(&tmp, reserved, 4);
        EMIT_MAP_END();
      }
      break;
    }
#endif /* V4L2_CTRL_TYPE_AV1_FILM_GRAIN */
      assert(0);
      break;
    default:
      MCTK_PANIC("Unknown control type");
      break;
  }
  // This function is large, but that's how it is now.
  // If YAML support is kept, then this should be rewritten using YamlNode
  // anyway. NOLINTNEXTLINE(readability/fn_size)
}

void EmitPad(yaml_emitter_t& emitter, V4lMcPad* pad) {
  assert(pad);

  EMIT_MAP_START();
  EMIT_KEY_CSTR("desc");
  EMIT_MAP_START();
  EMIT_U32_ELEM(&pad->desc_, entity);
  EMIT_U32_ELEM(&pad->desc_, index);
  EMIT_U32_ELEM(&pad->desc_, flags);
  EMIT_U32_ELEM_ARRAY(&pad->desc_, reserved, 2);
  EMIT_MAP_END();

  EMIT_KEY_CSTR("subdev_properties");
  EMIT_MAP_START();
  if (pad->subdev_.crop.has_value()) {
    EMIT_KEY_CSTR("crop");
    EMIT_RECT(&*pad->subdev_.crop);
  }

  if (pad->subdev_.fmt.has_value()) {
    EMIT_KEY_CSTR("fmt");
    EMIT_MAP_START();
    EMIT_U32_ELEM(&*pad->subdev_.fmt, width);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, height);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, code);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, field);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, colorspace);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, ycbcr_enc);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, quantization);
    EMIT_U32_ELEM(&*pad->subdev_.fmt, xfer_func);
#ifdef V4L2_MBUS_FRAMEFMT_SET_CSC
    EMIT_U32_ELEM(&*pad->subdev_.fmt, flags);
#endif /* V4L2_MBUS_FRAMEFMT_SET_CSC */
    EMIT_U32_ELEM_ARRAY(&*pad->subdev_.fmt, reserved, 10);
    EMIT_MAP_END();
  }

  if (pad->subdev_.frame_interval.has_value()) {
    EMIT_KEY_CSTR("frame_interval");
    EMIT_FRACT(&*pad->subdev_.frame_interval);
  }

  if (pad->subdev_.selection.HasAny()) {
    EMIT_KEY_CSTR("selection");
    EMIT_SELECTION(&pad->subdev_.selection);
  }
  EMIT_MAP_END();

  EMIT_KEY_CSTR("links");
  EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
  for (auto link : pad->links_) {
    /* Only outgoing links */
    EMIT_MAP_START();
    EMIT_KEY_CSTR("sink");
    EMIT_MAP_START();
    EMIT_U32_ELEM(&link->desc_.sink, entity);
    EMIT_U32_ELEM(&link->desc_.sink, index);
    EMIT_U32_ELEM(&link->desc_.sink, flags);
    EMIT_U32_ELEM_ARRAY(&link->desc_.sink, reserved, 2);
    EMIT_MAP_END();
    EMIT_U32_ELEM(&link->desc_, flags);
    EMIT_U32_ELEM_ARRAY(&link->desc_, reserved, 2);
    EMIT_MAP_END();
  }
  EMIT_SEQ_END();
  EMIT_MAP_END();
}

void EmitEntity(yaml_emitter_t& emitter, V4lMcEntity* entity) {
  assert(entity);

  EMIT_MAP_START();
  EMIT_KEY_CSTR("desc");
  EMIT_MAP_START();
  EMIT_U32_ELEM(&entity->desc_, id);
  EMIT_KEY_CSTR("name");
  EMIT_CSTR(entity->desc_.name);
  EMIT_U32_ELEM(&entity->desc_, type);
  EMIT_U32_ELEM(&entity->desc_, revision);
  EMIT_U32_ELEM(&entity->desc_, flags);
  EMIT_U32_ELEM(&entity->desc_, group_id);
  EMIT_U32_ELEM(&entity->desc_, pads);
  EMIT_U32_ELEM(&entity->desc_, links);
  EMIT_MAP_END();

  EMIT_KEY_CSTR("v4l_properties");
  EMIT_MAP_START();
  if (entity->maindev_.audio.has_value()) {
    EMIT_KEY_CSTR("audio");
    EMIT_MAP_START();
    struct v4l2_audio* audio = &*entity->maindev_.audio;
    EMIT_U32_ELEM(audio, index);
    EMIT_KEY_CSTR("name");
    EMIT_CSTR(audio->name);
    EMIT_U32_ELEM(audio, capability);
    EMIT_U32_ELEM(audio, mode);
    EMIT_U32_ELEM(audio, capability);
    EMIT_U32_ELEM_ARRAY(audio, reserved, 2);
    EMIT_MAP_END();
  }

  if (entity->maindev_.audout.has_value()) {
    EMIT_KEY_CSTR("audout");
    EMIT_MAP_START();
    struct v4l2_audioout* audout = &*entity->maindev_.audout;
    EMIT_U32_ELEM(audout, index);
    EMIT_KEY_CSTR("name");
    EMIT_CSTR(audout->name);
    EMIT_U32_ELEM(audout, capability);
    EMIT_U32_ELEM(audout, mode);
    EMIT_U32_ELEM(audout, capability);
    EMIT_U32_ELEM_ARRAY(audout, reserved, 2);
    EMIT_MAP_END();
  }

  if (entity->maindev_.crop_video_capture.has_value()) {
    EMIT_KEY_CSTR("crop_video_capture");
    EMIT_RECT(&*entity->maindev_.crop_video_capture);
  }
  if (entity->maindev_.crop_video_output.has_value()) {
    EMIT_KEY_CSTR("crop_video_output");
    EMIT_RECT(&*entity->maindev_.crop_video_output);
  }
  if (entity->maindev_.crop_video_overlay.has_value()) {
    EMIT_KEY_CSTR("crop_video_overlay");
    EMIT_RECT(&*entity->maindev_.crop_video_overlay);
  }
  if (entity->maindev_.crop_video_capture_mplane.has_value()) {
    EMIT_KEY_CSTR("crop_video_capture_mplane");
    EMIT_RECT(&*entity->maindev_.crop_video_capture_mplane);
  }
  if (entity->maindev_.crop_video_output_mplane.has_value()) {
    EMIT_KEY_CSTR("crop_video_output_mplane");
    EMIT_RECT(&*entity->maindev_.crop_video_output_mplane);
  }

  if (entity->maindev_.dv_timings.has_value()) {
    EMIT_KEY_CSTR("dv_timings");
    EMIT_DV_TIMINGS(&*entity->maindev_.dv_timings);
  }

  if (entity->maindev_.subdev_dv_timings.has_value()) {
    EMIT_KEY_CSTR("subdev_dv_timings");
    EMIT_DV_TIMINGS(&*entity->maindev_.subdev_dv_timings);
  }

  /* Ignored: EDID */

  /* struct v4l2_framebuffer cannot be meaningfully serialised,
   * since it contains a pointer to a raw buffer.
   */

  /* VIDIOC_G_FMT */
  if (entity->maindev_.fmt_video_capture.has_value()) {
    EMIT_KEY_CSTR("fmt_video_capture");
    EMIT_MAP_START();
    struct v4l2_pix_format* fmt_video_capture =
        &*entity->maindev_.fmt_video_capture;
    EMIT_U32_ELEM(fmt_video_capture, width);
    EMIT_U32_ELEM(fmt_video_capture, height);
    EMIT_U32_ELEM(fmt_video_capture, pixelformat);
    EMIT_U32_ELEM(fmt_video_capture, bytesperline);
    EMIT_U32_ELEM(fmt_video_capture, sizeimage);
    EMIT_U32_ELEM(fmt_video_capture, colorspace);
    EMIT_U32_ELEM(fmt_video_capture, priv);
    EMIT_U32_ELEM(fmt_video_capture, flags);
    EMIT_U32_ELEM(fmt_video_capture, ycbcr_enc);
    EMIT_U32_ELEM(fmt_video_capture, quantization);
    EMIT_U32_ELEM(fmt_video_capture, xfer_func);
    /* No "reserved" element at the end of this struct. */
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_video_output.has_value()) {
    EMIT_KEY_CSTR("fmt_video_output");
    EMIT_MAP_START();
    struct v4l2_pix_format* fmt_video_output =
        &*entity->maindev_.fmt_video_output;
    EMIT_U32_ELEM(fmt_video_output, width);
    EMIT_U32_ELEM(fmt_video_output, height);
    EMIT_U32_ELEM(fmt_video_output, pixelformat);
    EMIT_U32_ELEM(fmt_video_output, bytesperline);
    EMIT_U32_ELEM(fmt_video_output, sizeimage);
    EMIT_U32_ELEM(fmt_video_output, colorspace);
    EMIT_U32_ELEM(fmt_video_output, priv);
    EMIT_U32_ELEM(fmt_video_output, flags);
    EMIT_U32_ELEM(fmt_video_output, ycbcr_enc);
    EMIT_U32_ELEM(fmt_video_output, quantization);
    EMIT_U32_ELEM(fmt_video_output, xfer_func);
    /* No "reserved" element at the end of this struct. */
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_video_overlay.has_value()) {
    /* v4l2_window is not (de)serialisable */
    assert(0);
  }
  if (entity->maindev_.fmt_vbi_capture.has_value()) {
    EMIT_KEY_CSTR("fmt_vbi_capture");
    EMIT_MAP_START();
    struct v4l2_vbi_format* fmt_vbi_capture =
        &*entity->maindev_.fmt_vbi_capture;
    EMIT_U32_ELEM(fmt_vbi_capture, sampling_rate);
    EMIT_U32_ELEM(fmt_vbi_capture, offset);
    EMIT_U32_ELEM(fmt_vbi_capture, samples_per_line);
    EMIT_U32_ELEM(fmt_vbi_capture, sample_format);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_capture, start, 2);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_capture, count, 2);
    EMIT_U32_ELEM(fmt_vbi_capture, flags);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_capture, reserved, 2);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_vbi_output.has_value()) {
    EMIT_KEY_CSTR("fmt_vbi_output");
    EMIT_MAP_START();
    struct v4l2_vbi_format* fmt_vbi_output = &*entity->maindev_.fmt_vbi_output;
    EMIT_U32_ELEM(fmt_vbi_output, sampling_rate);
    EMIT_U32_ELEM(fmt_vbi_output, offset);
    EMIT_U32_ELEM(fmt_vbi_output, samples_per_line);
    EMIT_U32_ELEM(fmt_vbi_output, sample_format);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_output, start, 2);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_output, count, 2);
    EMIT_U32_ELEM(fmt_vbi_output, flags);
    EMIT_U32_ELEM_ARRAY(fmt_vbi_output, reserved, 2);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_sliced_vbi_capture.has_value()) {
    /* This format is not finalised in the V4L2 API yet. */
    assert(0);
  }
  if (entity->maindev_.fmt_sliced_vbi_output.has_value()) {
    /* This format is not finalised in the V4L2 API yet. */
    assert(0);
  }
  if (entity->maindev_.fmt_video_output_overlay.has_value()) {
    /* v4l2_window is not (de)serialisable */
    assert(0);
  }
  if (entity->maindev_.fmt_video_capture_mplane.has_value()) {
    EMIT_KEY_CSTR("fmt_video_capture_mplane");
    EMIT_MAP_START();
    struct v4l2_pix_format_mplane* fmt_video_capture_mplane =
        &*entity->maindev_.fmt_video_capture_mplane;
    EMIT_U32_ELEM(fmt_video_capture_mplane, width);
    EMIT_U32_ELEM(fmt_video_capture_mplane, height);
    EMIT_U32_ELEM(fmt_video_capture_mplane, pixelformat);
    EMIT_U32_ELEM(fmt_video_capture_mplane, field);
    EMIT_U32_ELEM(fmt_video_capture_mplane, colorspace);
    EMIT_KEY_CSTR("plane_fmt");
    EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
    for (int i = 0; i < VIDEO_MAX_PLANES; i++) {
      EMIT_MAP_START();
      EMIT_U32_ELEM(&fmt_video_capture_mplane->plane_fmt[i], sizeimage);
      EMIT_U32_ELEM(&fmt_video_capture_mplane->plane_fmt[i], bytesperline);
      EMIT_MAP_END();
    }
    EMIT_SEQ_END();
    EMIT_U32_ELEM(fmt_video_capture_mplane, num_planes);
    EMIT_U32_ELEM(fmt_video_capture_mplane, flags);
    EMIT_U32_ELEM(fmt_video_capture_mplane, ycbcr_enc);
    EMIT_U32_ELEM(fmt_video_capture_mplane, quantization);
    EMIT_U32_ELEM(fmt_video_capture_mplane, xfer_func);
    EMIT_U32_ELEM_ARRAY(fmt_video_capture_mplane, reserved, 7);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_video_output_mplane.has_value()) {
    EMIT_KEY_CSTR("fmt_video_output_mplane");
    EMIT_MAP_START();
    struct v4l2_pix_format_mplane* fmt_video_output_mplane =
        &*entity->maindev_.fmt_video_output_mplane;
    EMIT_U32_ELEM(fmt_video_output_mplane, width);
    EMIT_U32_ELEM(fmt_video_output_mplane, height);
    EMIT_U32_ELEM(fmt_video_output_mplane, pixelformat);
    EMIT_U32_ELEM(fmt_video_output_mplane, field);
    EMIT_U32_ELEM(fmt_video_output_mplane, colorspace);
    EMIT_KEY_CSTR("plane_fmt");
    EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
    for (int i = 0; i < VIDEO_MAX_PLANES; i++) {
      EMIT_MAP_START();
      EMIT_U32_ELEM(&fmt_video_output_mplane->plane_fmt[i], sizeimage);
      EMIT_U32_ELEM(&fmt_video_output_mplane->plane_fmt[i], bytesperline);
      EMIT_MAP_END();
    }
    EMIT_SEQ_END();
    EMIT_U32_ELEM(fmt_video_output_mplane, num_planes);
    EMIT_U32_ELEM(fmt_video_output_mplane, flags);
    EMIT_U32_ELEM(fmt_video_output_mplane, ycbcr_enc);
    EMIT_U32_ELEM(fmt_video_output_mplane, quantization);
    EMIT_U32_ELEM(fmt_video_output_mplane, xfer_func);
    EMIT_U32_ELEM_ARRAY(fmt_video_output_mplane, reserved, 7);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_sdr_capture.has_value()) {
    EMIT_KEY_CSTR("fmt_sdr_capture");
    EMIT_MAP_START();
    struct v4l2_sdr_format* fmt_sdr_capture =
        &*entity->maindev_.fmt_sdr_capture;
    EMIT_U32_ELEM(fmt_sdr_capture, pixelformat);
    EMIT_U32_ELEM(fmt_sdr_capture, buffersize);
    EMIT_U32_ELEM_ARRAY(fmt_sdr_capture, reserved, 24);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_sdr_output.has_value()) {
    EMIT_KEY_CSTR("fmt_sdr_output");
    EMIT_MAP_START();
    struct v4l2_sdr_format* fmt_sdr_output = &*entity->maindev_.fmt_sdr_output;
    EMIT_U32_ELEM(fmt_sdr_output, pixelformat);
    EMIT_U32_ELEM(fmt_sdr_output, buffersize);
    EMIT_U32_ELEM_ARRAY(fmt_sdr_output, reserved, 24);
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_meta_capture.has_value()) {
    EMIT_KEY_CSTR("fmt_meta_capture");
    EMIT_MAP_START();
    struct v4l2_meta_format* fmt_meta_capture =
        &*entity->maindev_.fmt_meta_capture;
    EMIT_U32_ELEM(fmt_meta_capture, dataformat);
    EMIT_U32_ELEM(fmt_meta_capture, buffersize);
    /* No "reserved" element at the end of this struct. */
    EMIT_MAP_END();
  }
  if (entity->maindev_.fmt_meta_output.has_value()) {
    EMIT_KEY_CSTR("fmt_meta_output");
    EMIT_MAP_START();
    struct v4l2_meta_format* fmt_meta_output =
        &*entity->maindev_.fmt_meta_output;
    EMIT_U32_ELEM(fmt_meta_output, dataformat);
    EMIT_U32_ELEM(fmt_meta_output, buffersize);
    /* No "reserved" element at the end of this struct. */
    EMIT_MAP_END();
  }

  /* Ignored: Frequency */

  if (entity->maindev_.input.has_value()) {
    EMIT_KEY_CSTR("input");
    EMIT_S32(*entity->maindev_.input);
  }

  if (entity->maindev_.jpegcomp.has_value()) {
    EMIT_KEY_CSTR("jpegcomp");
    EMIT_MAP_START();
    struct v4l2_jpegcompression* jpegcomp = &*entity->maindev_.jpegcomp;
    EMIT_S32_ELEM(jpegcomp, quality);
    EMIT_S32_ELEM(jpegcomp, APPn);
    EMIT_S32_ELEM(jpegcomp, APP_len);
    EMIT_U32_ELEM_ARRAY(jpegcomp, APP_data, 60);
    EMIT_S32_ELEM(jpegcomp, COM_len);
    EMIT_U32_ELEM_ARRAY(jpegcomp, COM_data, 60);
    EMIT_U32_ELEM(jpegcomp, jpeg_markers);
    /* No "reserved" element at the end of this struct. */
    EMIT_MAP_END();
  }

  /* Ignored: Modulator */

  if (entity->maindev_.output.has_value()) {
    EMIT_KEY_CSTR("output");
    EMIT_S32(*entity->maindev_.output);
  }

  /* VIDIOC_G_PARM */
  if (entity->maindev_.parm_video_capture.has_value()) {
    EMIT_KEY_CSTR("parm_video_capture");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_video_capture);
  }
  if (entity->maindev_.parm_video_output.has_value()) {
    EMIT_KEY_CSTR("parm_video_output");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_video_output);
  }
  if (entity->maindev_.parm_video_overlay.has_value()) {
    EMIT_KEY_CSTR("parm_video_overlay");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_video_overlay);
  }
  if (entity->maindev_.parm_vbi_capture.has_value()) {
    EMIT_KEY_CSTR("parm_vbi_capture");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_vbi_capture);
  }
  if (entity->maindev_.parm_vbi_output.has_value()) {
    EMIT_KEY_CSTR("parm_vbi_output");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_vbi_output);
  }
  if (entity->maindev_.parm_sliced_vbi_capture.has_value()) {
    EMIT_KEY_CSTR("parm_sliced_vbi_capture");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_sliced_vbi_capture);
  }
  if (entity->maindev_.parm_sliced_vbi_output.has_value()) {
    EMIT_KEY_CSTR("parm_sliced_vbi_output");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_sliced_vbi_output);
  }
  if (entity->maindev_.parm_video_output_overlay.has_value()) {
    EMIT_KEY_CSTR("parm_video_output_overlay");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_video_output_overlay);
  }
  if (entity->maindev_.parm_video_capture_mplane.has_value()) {
    EMIT_KEY_CSTR("parm_video_capture_mplane");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_video_capture_mplane);
  }
  if (entity->maindev_.parm_video_output_mplane.has_value()) {
    EMIT_KEY_CSTR("parm_video_output_mplane");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_video_output_mplane);
  }
  if (entity->maindev_.parm_sdr_capture.has_value()) {
    EMIT_KEY_CSTR("parm_sdr_capture");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_sdr_capture);
  }
  if (entity->maindev_.parm_sdr_output.has_value()) {
    EMIT_KEY_CSTR("parm_sdr_output");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_sdr_output);
  }
  if (entity->maindev_.parm_meta_capture.has_value()) {
    EMIT_KEY_CSTR("parm_meta_capture");
    EMIT_CAPTUREPARM(&*entity->maindev_.parm_meta_capture);
  }
  if (entity->maindev_.parm_meta_output.has_value()) {
    EMIT_KEY_CSTR("parm_meta_output");
    EMIT_OUTPUTPARM(&*entity->maindev_.parm_meta_output);
  }

  if (entity->maindev_.priority.has_value()) {
    EMIT_KEY_CSTR("priority");
    EMIT_U32(*entity->maindev_.priority);
  }

  bool has_any_selection = false;
  for (int i = 1; i <= V4L2_BUF_TYPE_META_OUTPUT; i++) {
    if (entity->maindev_.selection[i - 1].HasAny()) {
      has_any_selection = true;
      break;
    }
  }
  if (has_any_selection) {
    EMIT_KEY_CSTR("selection");
    EMIT_MAP_START();
    for (int i = 1; i <= V4L2_BUF_TYPE_META_OUTPUT; i++) {
      if (!entity->maindev_.selection[i - 1].HasAny())
        continue;

      EMIT_U32(i); /* key */
      EMIT_SELECTION(&entity->maindev_.selection[i - 1]);
    }
    EMIT_MAP_END();
  }

  if (entity->maindev_.std.has_value()) {
    EMIT_KEY_CSTR("std");
    EMIT_U64(*entity->maindev_.std);
  }

  if (entity->maindev_.subdev_std.has_value()) {
    EMIT_KEY_CSTR("subdev_std");
    EMIT_U64(*entity->maindev_.subdev_std);
  }

  /* Ignored: Tuner */
  EMIT_MAP_END();

  if (!entity->controls_.empty()) {
    EMIT_KEY_CSTR("controls");
    EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
    for (auto& control : entity->controls_) {
      EMIT_MAP_START();
      EMIT_KEY_CSTR("desc");
      EMIT_MAP_START();
      EMIT_U32_ELEM(&control->desc_, id);
      EMIT_U32_ELEM(&control->desc_, type);
      EMIT_KEY_CSTR("name");
      EMIT_CSTR(control->desc_.name);
      EMIT_S64_ELEM(&control->desc_, minimum);
      EMIT_S64_ELEM(&control->desc_, maximum);
      EMIT_U64_ELEM(&control->desc_, step);
      EMIT_S64_ELEM(&control->desc_, default_value);
      EMIT_U32_ELEM(&control->desc_, flags);
      EMIT_U32_ELEM(&control->desc_, elem_size);
      if (control->desc_.nr_of_dims)
        EMIT_U32_ELEM_ARRAY(&control->desc_, dims, control->desc_.nr_of_dims);
      EMIT_U32_ELEM_ARRAY(&control->desc_, reserved, 32);
      EMIT_MAP_END();
      EMIT_KEY_CSTR("values");
      EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
      EmitControlValues(emitter, *control);
      EMIT_SEQ_END();

      EMIT_MAP_END();
    }
    EMIT_SEQ_END();
  }

  EMIT_KEY_CSTR("pads");
  EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
  for (auto& pad : entity->pads_) {
    EmitPad(emitter, pad.get());
  }
  EMIT_SEQ_END();
  EMIT_MAP_END();
}

void EmitMc(yaml_emitter_t& emitter, V4lMcDev& mcdev) {
  EMIT_KEY_CSTR("media_ctl");
  EMIT_MAP_START();
  EMIT_KEY_CSTR("info");
  EMIT_MAP_START();
  EMIT_KEY_CSTR("driver");
  EMIT_CSTR(mcdev.info_.driver);
  EMIT_KEY_CSTR("model");
  EMIT_CSTR(mcdev.info_.model);
  EMIT_KEY_CSTR("serial");
  EMIT_CSTR(mcdev.info_.serial);
  EMIT_KEY_CSTR("bus_info");
  EMIT_CSTR(mcdev.info_.bus_info);
  EMIT_U32_ELEM(&mcdev.info_, media_version);
  EMIT_U32_ELEM(&mcdev.info_, hw_revision);
  EMIT_U32_ELEM(&mcdev.info_, driver_version);
  EMIT_MAP_END();

  EMIT_KEY_CSTR("entities");
  EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
  for (auto& entity : mcdev.entities_) {
    EmitEntity(emitter, entity.get());
  }
  EMIT_SEQ_END();
  EMIT_MAP_END();
}

void EmitRemap(yaml_emitter_t& emitter, V4lMcDev& mcdev) {
  EMIT_KEY_CSTR("remap_entity_by_name");
  EMIT_SEQ_START(YAML_BLOCK_SEQUENCE_STYLE);
  for (auto& entity : mcdev.entities_) {
    EMIT_MAP_START();
    EMIT_U32_ELEM(&entity->desc_, id);
    EMIT_KEY_CSTR("name");
    EMIT_CSTR(entity->desc_.name);
    EMIT_MAP_END();
  }
  EMIT_SEQ_END();
}

}  // namespace

void V4lMcDev::ToYamlFile(FILE& file) {
  yaml_emitter_t emitter;

  yaml_emitter_initialize(&emitter);
  yaml_emitter_set_output_file(&emitter, &file);

  EMIT_STREAM_START();
  EMIT_DOCUMENT_START();

  EMIT_MAP_START();

  EmitRemap(emitter, *this);
  EmitMc(emitter, *this);

  EMIT_MAP_END();

  EMIT_DOCUMENT_END();
  EMIT_STREAM_END();

  yaml_emitter_delete(&emitter);
}
