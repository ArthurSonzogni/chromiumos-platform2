// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

use v4l2r::bindings::v4l2_ctrl_hevc_decode_params;
use v4l2r::bindings::v4l2_ctrl_hevc_pps;
use v4l2r::bindings::v4l2_ctrl_hevc_scaling_matrix;
use v4l2r::bindings::v4l2_ctrl_hevc_sps;
use v4l2r::bindings::v4l2_ext_control;
use v4l2r::bindings::v4l2_ext_control__bindgen_ty_1;
use v4l2r::bindings::v4l2_hevc_dpb_entry;
use v4l2r::bindings::v4l2_stateless_hevc_decode_mode_V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED as V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED;
use v4l2r::bindings::v4l2_stateless_hevc_decode_mode_V4L2_STATELESS_HEVC_DECODE_MODE_SLICE_BASED as V4L2_STATELESS_HEVC_DECODE_MODE_SLICE_BASED;
use v4l2r::bindings::v4l2_stateless_hevc_start_code_V4L2_STATELESS_HEVC_START_CODE_ANNEX_B as V4L2_STATELESS_HEVC_START_CODE_ANNEX_B;
use v4l2r::bindings::v4l2_stateless_hevc_start_code_V4L2_STATELESS_HEVC_START_CODE_NONE as V4L2_STATELESS_HEVC_START_CODE_NONE;
use v4l2r::bindings::V4L2_CID_STATELESS_HEVC_DECODE_PARAMS;
use v4l2r::bindings::V4L2_CID_STATELESS_HEVC_PPS;
use v4l2r::bindings::V4L2_CID_STATELESS_HEVC_SCALING_MATRIX;
use v4l2r::bindings::V4L2_CID_STATELESS_HEVC_SPS;
use v4l2r::bindings::V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC;
use v4l2r::bindings::V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC;
use v4l2r::bindings::V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR;
use v4l2r::bindings::V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE;
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
use v4l2r::bindings::V4L2_HEVC_SEI_PIC_STRUCT_FRAME;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_AMP_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_PCM_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED;
use v4l2r::bindings::V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED;
use v4l2r::controls::AsV4l2ControlSlice;

use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderHandle;
use crate::codec::h265::parser::NaluType;
use crate::codec::h265::parser::Pps;
use crate::codec::h265::parser::SliceHeader;
use crate::codec::h265::parser::Sps;
use crate::codec::h265::picture::PictureData;
use crate::decoder::stateless::h265::RefPicSet;
use crate::video_frame::VideoFrame;

// Defined in 7.4.5
const SCALING_LIST_SIZE_1_TO_3_COUNT: usize = 64;
const SCALING_LIST_SIZE_0_COUNT: usize = 16;

const RASTER_SCAN_ORDER_4X4: [usize; 16] = [0, 2, 5, 9, 1, 4, 8, 12, 3, 7, 11, 14, 6, 10, 13, 15];
const RASTER_SCAN_ORDER_8X8: [usize; 64] = [
    0, 2, 5, 9, 14, 20, 27, 35, 1, 4, 8, 13, 19, 26, 34, 42, 3, 7, 12, 18, 25, 33, 41, 48, 6, 11,
    17, 24, 32, 40, 47, 53, 10, 16, 23, 31, 39, 46, 52, 57, 15, 22, 30, 38, 45, 51, 56, 60, 21, 29,
    37, 44, 50, 55, 59, 62, 28, 36, 43, 49, 54, 58, 61, 63,
];

fn get_scaling_in_raster_order_4x4(
    matrix_id: usize,
    raster_idx: usize,
    scaling_list: &[[u8; 16]; 6],
) -> u8 {
    let up_right_diag_idx: usize = RASTER_SCAN_ORDER_4X4[raster_idx];
    scaling_list[matrix_id][up_right_diag_idx]
}

fn get_scaling_in_raster_order_8x8(
    matrix_id: usize,
    raster_idx: usize,
    scaling_list: &[[u8; 64]; 6],
) -> u8 {
    let up_right_diag_idx: usize = RASTER_SCAN_ORDER_8X8[raster_idx];
    scaling_list[matrix_id][up_right_diag_idx]
}

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

impl From<&Pps> for v4l2_ctrl_hevc_scaling_matrix {
    fn from(pps: &Pps) -> Self {
        let mut scaling_list_4x4 = [[0; 16]; 6];
        let mut scaling_list_8x8 = [[0; 64]; 6];
        let mut scaling_list_16x16 = [[0; 64]; 6];
        let mut scaling_list_32x32 = [[0; 64]; 2];
        let mut scaling_list_dc_coef_16x16 = [0; 6];
        let mut scaling_list_dc_coef_32x32 = [0; 2];

        for i in 0..6 {
            for j in 0..SCALING_LIST_SIZE_1_TO_3_COUNT {
                if j < SCALING_LIST_SIZE_0_COUNT {
                    scaling_list_4x4[i][j] =
                        get_scaling_in_raster_order_4x4(i, j, &pps.scaling_list.scaling_list_4x4);
                }
                scaling_list_8x8[i][j] =
                    get_scaling_in_raster_order_8x8(i, j, &pps.scaling_list.scaling_list_8x8);
                scaling_list_16x16[i][j] =
                    get_scaling_in_raster_order_8x8(i, j, &pps.scaling_list.scaling_list_16x16);
            }
        }

        for i in (0..6).step_by(3) {
            for j in 0..SCALING_LIST_SIZE_1_TO_3_COUNT {
                scaling_list_32x32[i / 3][j] =
                    get_scaling_in_raster_order_8x8(i, j, &pps.scaling_list.scaling_list_32x32);
            }
        }

        for i in 0..6 {
            scaling_list_dc_coef_16x16[i] =
                pps.scaling_list.scaling_list_dc_coef_minus8_16x16[i] as u8;
        }
        scaling_list_dc_coef_32x32[0] = pps.scaling_list.scaling_list_dc_coef_minus8_32x32[0] as u8;
        scaling_list_dc_coef_32x32[1] = pps.scaling_list.scaling_list_dc_coef_minus8_32x32[3] as u8;

        Self {
            scaling_list_4x4,
            scaling_list_8x8,
            scaling_list_16x16,
            scaling_list_32x32,
            scaling_list_dc_coef_16x16,
            scaling_list_dc_coef_32x32,
        }
    }
}

pub struct V4l2CtrlHEVCDpbEntry {
    pub timestamp: u64,
    pub pic: PictureData,
}

impl From<&V4l2CtrlHEVCDpbEntry> for v4l2_hevc_dpb_entry {
    fn from(dpb: &V4l2CtrlHEVCDpbEntry) -> Self {
        let pic: &PictureData = &dpb.pic;

        let mut flags: u32 = 0;
        if pic.is_long_term() {
            flags = V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE;
        }

        Self {
            timestamp: dpb.timestamp * 1000, // usec to nsec
            flags: flags as u8,
            field_pic: V4L2_HEVC_SEI_PIC_STRUCT_FRAME as u8,
            pic_order_cnt_val: pic.pic_order_cnt_val,
            ..Default::default()
        }
    }
}

#[derive(Default)]
pub struct V4l2CtrlHEVCDecodeParams {
    handle: v4l2_ctrl_hevc_decode_params,
}

impl V4l2CtrlHEVCDecodeParams {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn set_ref_pic_set<V: VideoFrame>(
        &mut self,
        mut rps: RefPicSet<V4l2StatelessDecoderHandle<V>>,
    ) -> &mut Self {
        let mut i = 0;
        for pic in rps.get_ref_pic_set_st_curr_before() {
            if pic.is_some() == false {
                continue;
            }
            let pic_entry = pic.unwrap().0.clone();
            for j in 0..self.handle.num_active_dpb_entries {
                if pic_entry.as_ref().clone().into_inner().pic_order_cnt_val
                    == self.handle.dpb[j as usize].pic_order_cnt_val
                {
                    self.handle.poc_st_curr_before[i as usize] = j;
                    i += 1;
                }
            }
        }
        self.handle.num_poc_st_curr_before = i;

        i = 0;
        for pic in rps.get_ref_pic_set_st_curr_after() {
            if pic.is_some() == false {
                continue;
            }
            let pic_entry = pic.unwrap().0.clone();
            for j in 0..self.handle.num_active_dpb_entries {
                if pic_entry.as_ref().clone().into_inner().pic_order_cnt_val
                    == self.handle.dpb[j as usize].pic_order_cnt_val
                {
                    self.handle.poc_st_curr_after[i as usize] = j;
                    i += 1;
                }
            }
        }
        self.handle.num_poc_st_curr_after = i;

        i = 0;
        for pic in rps.get_ref_pic_set_lt_curr() {
            if pic.is_some() == false {
                continue;
            }
            let pic_entry = pic.unwrap().0.clone();
            for j in 0..self.handle.num_active_dpb_entries {
                if pic_entry.as_ref().clone().into_inner().pic_order_cnt_val
                    == self.handle.dpb[j as usize].pic_order_cnt_val
                {
                    self.handle.poc_lt_curr[i as usize] = j;
                    i += 1;
                }
            }
        }
        self.handle.num_poc_lt_curr = i;

        self
    }

    pub fn handle(&self) -> v4l2_ctrl_hevc_decode_params {
        self.handle
    }

    pub fn set_picture_data(&mut self, pic: &PictureData) -> &mut Self {
        self.handle.pic_order_cnt_val = pic.pic_order_cnt_val;
        if (pic.nalu_type >= NaluType::IdrWRadl) && (pic.nalu_type <= NaluType::IdrNLp) {
            self.handle.flags |= V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC as u64;
        }
        if pic.no_output_of_prior_pics_flag {
            self.handle.flags |= V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR as u64;
        }
        if (pic.nalu_type >= NaluType::BlaWLp) && (pic.nalu_type <= NaluType::RsvIrapVcl23) {
            self.handle.flags |= V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC as u64;
        }

        self
    }

    pub fn set_dpb_entries(&mut self, dpb: Vec<V4l2CtrlHEVCDpbEntry>) -> &mut Self {
        for i in 0..dpb.len() {
            self.handle.dpb[i] = v4l2_hevc_dpb_entry::from(&dpb[i]);
        }
        self.handle.num_active_dpb_entries = dpb.len() as u8;
        self
    }

    pub fn set_slice_header(&mut self, slice_header: &SliceHeader) -> &mut Self {
        self.handle.short_term_ref_pic_set_size = slice_header.st_rps_bits as u16;
        self.handle.long_term_ref_pic_set_size = slice_header.num_long_term_pics as u16;
        self.handle.num_delta_pocs_of_ref_rps_idx =
            slice_header.short_term_ref_pic_set.num_delta_pocs as u8;

        self
    }
}

pub enum V4l2CtrlHEVCDecodeMode {
    SliceBased = V4L2_STATELESS_HEVC_DECODE_MODE_SLICE_BASED as isize,
    FrameBased = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED as isize,
}

pub enum V4l2CtrlHEVCStartCode {
    None = V4L2_STATELESS_HEVC_START_CODE_NONE as isize,
    AnnexB = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B as isize,
}

pub struct HevcV4l2Sps(v4l2_ext_control, PhantomData<v4l2_ctrl_hevc_sps>);
impl From<&v4l2_ctrl_hevc_sps> for HevcV4l2Sps {
    fn from(sps: &v4l2_ctrl_hevc_sps) -> Self {
        let payload = Box::new(*sps);
        Self(
            v4l2_ext_control {
                id: V4L2_CID_STATELESS_HEVC_SPS,
                size: std::mem::size_of::<v4l2_ctrl_hevc_sps>() as u32,
                __bindgen_anon_1: v4l2_ext_control__bindgen_ty_1 {
                    p_hevc_sps: Box::into_raw(payload),
                },
                ..Default::default()
            },
            PhantomData,
        )
    }
}
impl AsV4l2ControlSlice for &mut HevcV4l2Sps {
    fn as_v4l2_control_slice(&mut self) -> &mut [v4l2_ext_control] {
        std::slice::from_mut(&mut self.0)
    }
}
impl Drop for HevcV4l2Sps {
    fn drop(&mut self) {
        // SAFETY: p_hevc_sps contains a pointer to a non-NULL v4l2_ctrl_hevc_sps object.
        unsafe {
            let _ = Box::from_raw(self.0.__bindgen_anon_1.p_hevc_sps);
        }
    }
}

pub struct HevcV4l2Pps(v4l2_ext_control, PhantomData<v4l2_ctrl_hevc_pps>);
impl From<&v4l2_ctrl_hevc_pps> for HevcV4l2Pps {
    fn from(pps: &v4l2_ctrl_hevc_pps) -> Self {
        let payload = Box::new(*pps);
        Self(
            v4l2_ext_control {
                id: V4L2_CID_STATELESS_HEVC_PPS,
                size: std::mem::size_of::<v4l2_ctrl_hevc_pps>() as u32,
                __bindgen_anon_1: v4l2_ext_control__bindgen_ty_1 {
                    p_hevc_pps: Box::into_raw(payload),
                },
                ..Default::default()
            },
            PhantomData,
        )
    }
}
impl AsV4l2ControlSlice for &mut HevcV4l2Pps {
    fn as_v4l2_control_slice(&mut self) -> &mut [v4l2_ext_control] {
        std::slice::from_mut(&mut self.0)
    }
}
impl Drop for HevcV4l2Pps {
    fn drop(&mut self) {
        // SAFETY: p_hevc_pps contains a pointer to a non-NULL v4l2_ctrl_hevc_pps object.
        unsafe {
            let _ = Box::from_raw(self.0.__bindgen_anon_1.p_hevc_pps);
        }
    }
}

pub struct HevcV4l2ScalingMatrix(v4l2_ext_control, PhantomData<v4l2_ctrl_hevc_scaling_matrix>);
impl From<&v4l2_ctrl_hevc_scaling_matrix> for HevcV4l2ScalingMatrix {
    fn from(mat: &v4l2_ctrl_hevc_scaling_matrix) -> Self {
        let payload = Box::new(*mat);
        Self(
            v4l2_ext_control {
                id: V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
                size: std::mem::size_of::<v4l2_ctrl_hevc_scaling_matrix>() as u32,
                __bindgen_anon_1: v4l2_ext_control__bindgen_ty_1 {
                    p_hevc_scaling_matrix: Box::into_raw(payload),
                },
                ..Default::default()
            },
            PhantomData,
        )
    }
}
impl AsV4l2ControlSlice for &mut HevcV4l2ScalingMatrix {
    fn as_v4l2_control_slice(&mut self) -> &mut [v4l2_ext_control] {
        std::slice::from_mut(&mut self.0)
    }
}
impl Drop for HevcV4l2ScalingMatrix {
    fn drop(&mut self) {
        // SAFETY: p_hevc_scaling_matrix contains a pointer to a non-NULL v4l2_ctrl_hevc_scaling_matrix object.
        unsafe {
            let _ = Box::from_raw(self.0.__bindgen_anon_1.p_hevc_scaling_matrix);
        }
    }
}

pub struct HevcV4l2DecodeParams(v4l2_ext_control, PhantomData<v4l2_ctrl_hevc_decode_params>);
impl From<&v4l2_ctrl_hevc_decode_params> for HevcV4l2DecodeParams {
    fn from(params: &v4l2_ctrl_hevc_decode_params) -> Self {
        let payload = Box::new(*params);
        Self(
            v4l2_ext_control {
                id: V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
                size: std::mem::size_of::<v4l2_ctrl_hevc_decode_params>() as u32,
                __bindgen_anon_1: v4l2_ext_control__bindgen_ty_1 {
                    p_hevc_decode_params: Box::into_raw(payload),
                },
                ..Default::default()
            },
            PhantomData,
        )
    }
}
impl AsV4l2ControlSlice for &mut HevcV4l2DecodeParams {
    fn as_v4l2_control_slice(&mut self) -> &mut [v4l2_ext_control] {
        std::slice::from_mut(&mut self.0)
    }
}
impl Drop for HevcV4l2DecodeParams {
    fn drop(&mut self) {
        // SAFETY: p_hevc_decode_params contains a pointer to a non-NULL v4l2_ctrl_hevc_decode_params object.
        unsafe {
            let _ = Box::from_raw(self.0.__bindgen_anon_1.p_hevc_decode_params);
        }
    }
}
