// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use v4l2r::bindings::v4l2_ctrl_hevc_pps;
use v4l2r::bindings::v4l2_ctrl_hevc_sps;

use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_AMP_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_PCM_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED;

use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_TILES_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED;
use v4l2r::bindings::V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED;

use crate::codec::h265::parser::Pps;
use crate::codec::h265::parser::Sps;

impl From<&Sps> for v4l2_ctrl_hevc_sps {
    fn from(sps: &Sps) -> Self {
        let mut flags: u64 = 0;

        if sps.separate_colour_plane_flag {
            flags |= V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE as u64;
        }
        if sps.scaling_list_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED as u64;
        }
        if sps.amp_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_AMP_ENABLED as u64;
        }
        if sps.sample_adaptive_offset_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET as u64;
        }
        if sps.pcm_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_PCM_ENABLED as u64;
        }
        if sps.pcm_loop_filter_disabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED as u64;
        }
        if sps.long_term_ref_pics_present_flag {
            flags |= V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT as u64;
        }
        if sps.temporal_mvp_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED as u64;
        }
        if sps.strong_intra_smoothing_enabled_flag {
            flags |= V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED as u64;
        }

        let highest_tid = sps.max_sub_layers_minus1 as usize;
        Self {
            video_parameter_set_id: sps.video_parameter_set_id,
            seq_parameter_set_id: sps.seq_parameter_set_id,
            //
            pic_width_in_luma_samples: sps.pic_width_in_luma_samples,
            pic_height_in_luma_samples: sps.pic_height_in_luma_samples,
            bit_depth_luma_minus8: sps.bit_depth_luma_minus8,
            bit_depth_chroma_minus8: sps.bit_depth_chroma_minus8,
            log2_max_pic_order_cnt_lsb_minus4: sps.log2_max_pic_order_cnt_lsb_minus4,
            //
            sps_max_dec_pic_buffering_minus1: sps.max_dec_pic_buffering_minus1[highest_tid],
            sps_max_num_reorder_pics: sps.max_num_reorder_pics[highest_tid],
            sps_max_latency_increase_plus1: sps.max_latency_increase_plus1[highest_tid],
            //
            log2_min_luma_coding_block_size_minus3: sps.log2_min_luma_coding_block_size_minus3,
            log2_diff_max_min_luma_coding_block_size: sps.log2_diff_max_min_luma_coding_block_size,
            log2_min_luma_transform_block_size_minus2: sps
                .log2_min_luma_transform_block_size_minus2,
            log2_diff_max_min_luma_transform_block_size: sps
                .log2_diff_max_min_luma_transform_block_size,
            max_transform_hierarchy_depth_inter: sps.max_transform_hierarchy_depth_inter,
            max_transform_hierarchy_depth_intra: sps.max_transform_hierarchy_depth_intra,
            pcm_sample_bit_depth_luma_minus1: sps.pcm_sample_bit_depth_luma_minus1,
            pcm_sample_bit_depth_chroma_minus1: sps.pcm_sample_bit_depth_chroma_minus1,
            log2_min_pcm_luma_coding_block_size_minus3: sps
                .log2_min_pcm_luma_coding_block_size_minus3,
            log2_diff_max_min_pcm_luma_coding_block_size: sps
                .log2_diff_max_min_pcm_luma_coding_block_size,
            num_short_term_ref_pic_sets: sps.num_short_term_ref_pic_sets,
            num_long_term_ref_pics_sps: sps.num_long_term_ref_pics_sps,
            chroma_format_idc: sps.chroma_format_idc,
            sps_max_sub_layers_minus1: sps.max_sub_layers_minus1,
            //
            flags,
            ..Default::default()
        }
    }
}

impl From<&Pps> for v4l2_ctrl_hevc_pps {
    fn from(pps: &Pps) -> Self {
        let mut flags: u64 = 0;

        if pps.dependent_slice_segments_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED as u64;
        }
        if pps.output_flag_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT as u64;
        }
        if pps.sign_data_hiding_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED as u64;
        }
        if pps.cabac_init_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT as u64;
        }
        if pps.constrained_intra_pred_flag {
            flags |= V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED as u64;
        }
        if pps.transform_skip_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED as u64;
        }
        if pps.cu_qp_delta_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED as u64;
        }
        if pps.slice_chroma_qp_offsets_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT as u64;
        }
        if pps.weighted_pred_flag {
            flags |= V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED as u64;
        }
        if pps.weighted_bipred_flag {
            flags |= V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED as u64;
        }
        if pps.transquant_bypass_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED as u64;
        }
        if pps.tiles_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_TILES_ENABLED as u64;
        }
        if pps.entropy_coding_sync_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED as u64;
        }
        if pps.loop_filter_across_tiles_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED as u64;
        }
        if pps.loop_filter_across_slices_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED as u64;
        }
        if pps.deblocking_filter_override_enabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED as u64;
        }
        if pps.deblocking_filter_disabled_flag {
            flags |= V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER as u64;
        }
        if pps.lists_modification_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT as u64;
        }
        if pps.slice_segment_header_extension_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT as u64;
        }
        if pps.deblocking_filter_control_present_flag {
            flags |= V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT as u64;
        }
        if pps.uniform_spacing_flag {
            flags |= V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING as u64;
        }

        let mut ret = Self {
            pic_parameter_set_id: pps.pic_parameter_set_id,
            num_extra_slice_header_bits: pps.num_extra_slice_header_bits,
            num_ref_idx_l0_default_active_minus1: pps.num_ref_idx_l0_default_active_minus1,
            num_ref_idx_l1_default_active_minus1: pps.num_ref_idx_l1_default_active_minus1,
            init_qp_minus26: pps.init_qp_minus26,
            diff_cu_qp_delta_depth: pps.diff_cu_qp_delta_depth,
            pps_cb_qp_offset: pps.cb_qp_offset,
            pps_cr_qp_offset: pps.cr_qp_offset,
            pps_beta_offset_div2: pps.beta_offset_div2,
            pps_tc_offset_div2: pps.tc_offset_div2,
            log2_parallel_merge_level_minus2: pps.log2_parallel_merge_level_minus2,
            flags,
            ..Default::default()
        };

        if pps.tiles_enabled_flag {
            ret.num_tile_columns_minus1 = pps.num_tile_columns_minus1;
            ret.num_tile_rows_minus1 = pps.num_tile_rows_minus1;

            if !pps.uniform_spacing_flag {
                for i in 0..=pps.num_tile_columns_minus1 {
                    ret.column_width_minus1[i as usize] = pps.column_width_minus1[i as usize] as u8;
                }
                for i in 0..=pps.num_tile_rows_minus1 {
                    ret.row_height_minus1[i as usize] = pps.row_height_minus1[i as usize] as u8;
                }
            }
        }

        ret
    }
}
