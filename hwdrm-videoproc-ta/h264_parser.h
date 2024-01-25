// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWDRM_VIDEOPROC_TA_H264_PARSER_H_
#define HWDRM_VIDEOPROC_TA_H264_PARSER_H_

#include <stdbool.h>
#include <stdint.h>

struct H264SliceHeaderData {
  uint8_t nal_ref_idc;
  uint8_t idr_pic_flag;
  uint8_t slice_type;
  uint8_t field_pic_flag;
  uint32_t frame_num;
  uint32_t idr_pic_id;
  uint32_t pic_order_cnt_lsb;
  int32_t delta_pic_order_cnt_bottom;
  int32_t delta_pic_order_cnt0;
  int32_t delta_pic_order_cnt1;
  union {
    struct {
      uint32_t no_output_of_prior_pics_flag : 1;
      uint32_t long_term_reference_flag : 1;
      uint32_t adaptive_ref_pic_marking_mode_flag : 1;
      uint32_t dec_ref_pic_marking_count : 8;
      uint32_t reserved : 21;
    } bits;
    uint32_t value;
  } ref_pic_fields;
  uint8_t memory_management_control_operation[32];
  int32_t difference_of_pic_nums_minus1[32];
  int32_t long_term_pic_num[32];
  int32_t max_long_term_frame_idx_plus1[32];
  int32_t long_term_frame_idx[32];
  uint32_t dec_ref_pic_marking_bit_size;
  uint32_t pic_order_cnt_bit_size;
};

// PPS and SPS fields needed to do slice header parsing.
struct StreamDataForSliceHeader {
  int32_t log2_max_frame_num_minus4;
  int32_t log2_max_pic_order_cnt_lsb_minus4;
  int32_t pic_order_cnt_type;
  int32_t num_ref_idx_l0_default_active_minus1;
  int32_t num_ref_idx_l1_default_active_minus1;
  int32_t weighted_bipred_idc;
  int32_t chroma_array_type;
  uint8_t frame_mbs_only_flag;
  uint8_t bottom_field_pic_order_in_frame_present_flag;
  uint8_t delta_pic_order_always_zero_flag;
  uint8_t redundant_pic_cnt_present_flag;
  uint8_t weighted_pred_flag;
  uint8_t padding[3];
};

bool ParseSliceHeader(const uint8_t* slice_header,
                      uint32_t header_size,
                      const struct StreamDataForSliceHeader* stream_data,
                      struct H264SliceHeaderData* hdr_out);

#endif  // HWDRM_VIDEOPROC_TA_H264_PARSER_H_
