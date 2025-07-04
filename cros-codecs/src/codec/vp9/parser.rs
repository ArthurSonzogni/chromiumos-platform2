// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bitstream_utils::BitReader;
use crate::codec::vp9::lookups::AC_QLOOKUP;
use crate::codec::vp9::lookups::AC_QLOOKUP_10;
use crate::codec::vp9::lookups::AC_QLOOKUP_12;
use crate::codec::vp9::lookups::DC_QLOOKUP;
use crate::codec::vp9::lookups::DC_QLOOKUP_10;
use crate::codec::vp9::lookups::DC_QLOOKUP_12;
use crate::ColorRange;

pub const REFS_PER_FRAME: usize = 3;

pub const MAX_REF_LF_DELTAS: usize = 4;
pub const MAX_MODE_LF_DELTAS: usize = 2;

pub const INTRA_FRAME: usize = 0;
pub const LAST_FRAME: usize = 1;
pub const GOLDEN_FRAME: usize = 2;
pub const ALTREF_FRAME: usize = 3;
pub const MAX_REF_FRAMES: usize = 4;

pub const MAX_SEGMENTS: usize = 8;
pub const SEG_TREE_PROBS: usize = MAX_SEGMENTS - 1;
pub const PREDICTION_PROBS: usize = 3;

/// Valid segment features values.
#[repr(u8)]
pub enum SegLvl {
    AltQ = 0,
    AltL = 1,
    RefFrame = 2,
    LvlSkip = 3,
}
pub const SEG_LVL_MAX: usize = 4;

pub const MAX_LOOP_FILTER: u32 = 63;

pub const REF_FRAMES_LOG2: usize = 3;
pub const REF_FRAMES: usize = 1 << REF_FRAMES_LOG2;

pub const SUPERFRAME_MARKER: u32 = 0x06;
pub const MAX_FRAMES_IN_SUPERFRAME: usize = 8;

pub const FRAME_MARKER: u32 = 0x02;
pub const SYNC_CODE: u32 = 0x498342;

pub const MIN_TILE_WIDTH_B64: u32 = 4;
pub const MAX_TILE_WIDTH_B64: u32 = 64;

/// The number of pictures in the DPB
pub const NUM_REF_FRAMES: usize = 8;

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum InterpolationFilter {
    #[default]
    EightTap = 0,
    EightTapSmooth = 1,
    EightTapSharp = 2,
    Bilinear = 3,
    Switchable = 4,
}

impl TryFrom<u32> for InterpolationFilter {
    type Error = String;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(InterpolationFilter::EightTap),
            1 => Ok(InterpolationFilter::EightTapSmooth),
            2 => Ok(InterpolationFilter::EightTapSharp),
            3 => Ok(InterpolationFilter::Bilinear),
            4 => Ok(InterpolationFilter::Switchable),
            _ => Err(format!("Invalid InterpolationFilter {}", value)),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ReferenceFrameType {
    Intra = 0,
    Last = 1,
    Golden = 2,
    AltRef = 3,
}

impl TryFrom<u32> for ReferenceFrameType {
    type Error = String;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(ReferenceFrameType::Intra),
            1 => Ok(ReferenceFrameType::Last),
            2 => Ok(ReferenceFrameType::Golden),
            3 => Ok(ReferenceFrameType::AltRef),
            _ => Err(format!("Invalid ReferenceFrameType {}", value)),
        }
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum FrameType {
    #[default]
    KeyFrame = 0,
    InterFrame = 1,
}

impl TryFrom<u8> for FrameType {
    type Error = String;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(FrameType::KeyFrame),
            1 => Ok(FrameType::InterFrame),
            _ => Err(format!("Invalid FrameType {}", value)),
        }
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum Profile {
    #[default]
    Profile0 = 0,
    Profile1 = 1,
    Profile2 = 2,
    Profile3 = 3,
}

impl TryFrom<u32> for Profile {
    type Error = String;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Profile::Profile0),
            1 => Ok(Profile::Profile1),
            2 => Ok(Profile::Profile2),
            3 => Ok(Profile::Profile3),
            _ => Err(format!("Invalid Profile {}", value)),
        }
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum BitDepth {
    #[default]
    Depth8 = 8,
    Depth10 = 10,
    Depth12 = 12,
}

impl TryFrom<u32> for BitDepth {
    type Error = String;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            8 => Ok(BitDepth::Depth8),
            10 => Ok(BitDepth::Depth10),
            12 => Ok(BitDepth::Depth12),
            _ => Err(format!("Invalid BitDepth {}", value)),
        }
    }
}

/// Note that this convention deviates from H264, H265, and AV1, so we need a separate enum here.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum ColorSpace {
    #[default]
    Unknown = 0,
    Bt601 = 1,
    Bt709 = 2,
    Smpte170 = 3,
    Smpte240 = 4,
    Bt2020 = 5,
    Reserved2 = 6,
    CsSrgb = 7,
}

impl TryFrom<u32> for ColorSpace {
    type Error = String;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(ColorSpace::Unknown),
            1 => Ok(ColorSpace::Bt601),
            2 => Ok(ColorSpace::Bt709),
            3 => Ok(ColorSpace::Smpte170),
            4 => Ok(ColorSpace::Smpte240),
            5 => Ok(ColorSpace::Bt2020),
            6 => Ok(ColorSpace::Reserved2),
            7 => Ok(ColorSpace::CsSrgb),
            _ => Err(format!("Invalid ColorSpace {}", value)),
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct LoopFilterParams {
    /// Indicates the loop filter strength.
    pub level: u8,
    /// Indicates the sharpness level. The loop filter level and loop
    /// filter_sharpness together determine when a block edge is filtered, and
    /// by how much the filtering can change the sample values.
    pub sharpness: u8,
    /// If set, means that the filter level depends on the mode and reference
    /// frame used to predict a block. If unset, means that the filter level
    /// does not depend on the mode and reference frame.
    pub delta_enabled: bool,
    /// If set, means that the bitstream contains additional syntax elements
    /// that specify which mode and reference frame deltas are to be updated. If
    /// unset, means that these syntax elements are not present.
    pub delta_update: bool,
    /// If set, means that the bitstream contains additional syntax elements
    /// that specify which mode and reference frame deltas are to be updated. If
    /// unset, means that these syntax elements are not present.
    pub update_ref_delta: [bool; MAX_REF_LF_DELTAS],
    /// Contains the adjustment needed for the filter level based on the chosen
    /// reference frame. If this syntax element is not present in the bitstream,
    /// it maintains its previous value.
    pub ref_deltas: [i8; MAX_REF_LF_DELTAS],
    ///  If set, means that the bitstream contains the syntax element
    ///  loop_filter_mode_deltas. If unset, means that the bitstream does not
    ///  contain this syntax element.
    pub update_mode_delta: [bool; MAX_MODE_LF_DELTAS],
    /// Contains the adjustment needed for the filter level based on the chosen
    /// mode. If this syntax element is not present in the bitstream, it
    /// maintains its previous value.
    pub mode_deltas: [i8; MAX_MODE_LF_DELTAS],
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct QuantizationParams {
    /// Indicates the base frame qindex. This is used for Y AC coefficients and
    /// as the base value for the other quantizers.
    pub base_q_idx: u8,
    /// Indicates the Y DC quantizer relative to base_q_idx.
    pub delta_q_y_dc: i8,
    /// Indicates the UV DC quantizer relative to base_q_idx.
    pub delta_q_uv_dc: i8,
    /// Indicates the UV AC quantizer relative to base_q_idx.
    pub delta_q_uv_ac: i8,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SegmentationParams {
    ///  If set, indicates that this frame makes use of the segmentation tool.
    ///  If unset, indicates that the frame does not use segmentation.
    pub enabled: bool,
    /// If set, indicates that the segmentation map should be updated during
    /// the decoding of this frame. If unset, means that the segmentation map
    /// from the previous frame is used.
    pub update_map: bool,
    /// Specify the probability values to be used when decoding segment_id.
    pub tree_probs: [u8; SEG_TREE_PROBS],
    /// Specify the probability values to be used when decoding seg_id_predicted.
    pub pred_probs: [u8; PREDICTION_PROBS],
    /// If set, indicates that the updates to the segmentation map are coded
    /// relative to the existing segmentation map. If unset,
    /// indicates that the new segmentation map is coded without
    /// reference to the existing segmentation map.
    pub temporal_update: bool,
    /// If set, indicates that new parameters are about to be specified for each
    /// segment. If unset, indicates that the segmentation parameters should
    /// keep their existing values.
    pub update_data: bool,
    /// If unset, indicates that the segmentation parameters represent
    /// adjustments relative to the standard values. If set, indicates that the
    /// segmentation parameters represent the actual values to be used.
    pub abs_or_delta_update: bool,
    /// If unset, indicates that the corresponding feature is unused and has
    /// value equal to 0. if set, indicates that the feature value is coded in
    /// the bitstream.
    pub feature_enabled: [[bool; SEG_LVL_MAX]; MAX_SEGMENTS],
    /// Specifies the magnitude of the feature data for a segment feature.
    pub feature_data: [[i16; SEG_LVL_MAX]; MAX_SEGMENTS],
}

impl SegmentationParams {
    /// Returns whether `feature` is enabled for `segment_id`.
    fn is_feature_enabled(&self, segment_id: u8, feature: SegLvl) -> bool {
        self.feature_enabled[segment_id as usize][feature as usize]
    }

    /// An implementation of seg_feature_active as per "6.4.9 Segmentation feature active syntax"
    fn is_feature_active(&self, segment_id: u8, feature: SegLvl) -> bool {
        self.enabled && self.is_feature_enabled(segment_id, feature)
    }

    /// Returns the data for `feature` on `segment_id`.
    fn feature_data(&self, segment_id: u8, feature: SegLvl) -> i16 {
        self.feature_data[segment_id as usize][feature as usize]
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Segmentation {
    /// Loop filter level
    pub lvl_lookup: [[u8; MAX_MODE_LF_DELTAS]; MAX_REF_FRAMES],

    /// AC quant scale for luma component
    pub luma_ac_quant_scale: i16,
    /// DC quant scale for luma component
    pub luma_dc_quant_scale: i16,
    /// AC quant scale for chroma component
    pub chroma_ac_quant_scale: i16,
    /// DC quant scale for chroma component
    pub chroma_dc_quant_scale: i16,

    /// Whether the alternate reference frame segment feature is enabled (SEG_LVL_REF_FRAME)
    pub reference_frame_enabled: bool,
    /// The feature data for the reference frame featire
    pub reference_frame: i16,
    /// Whether the skip segment feature is enabled (SEG_LVL_SKIP)
    pub reference_skip_enabled: bool,
}

impl Segmentation {
    /// Update the state of the segmentation parameters after seeing a frame
    pub fn update_segmentation(segmentation: &mut [Segmentation; MAX_SEGMENTS], hdr: &Header) {
        let lf = &hdr.lf;
        let seg = &hdr.seg;

        let n_shift = lf.level >> 5;

        for segment_id in 0..MAX_SEGMENTS as u8 {
            let luma_dc_quant_scale = hdr.get_dc_quant(segment_id, true);
            let luma_ac_quant_scale = hdr.get_ac_quant(segment_id, true);
            let chroma_dc_quant_scale = hdr.get_dc_quant(segment_id, false);
            let chroma_ac_quant_scale = hdr.get_ac_quant(segment_id, false);

            let mut lvl_lookup: [[u8; MAX_MODE_LF_DELTAS]; MAX_REF_FRAMES];

            if lf.level == 0 {
                lvl_lookup = Default::default()
            } else {
                let mut lvl_seg = i32::from(lf.level);

                // 8.8.1 Loop filter frame init process
                if hdr.seg.is_feature_active(segment_id, SegLvl::AltL) {
                    if seg.abs_or_delta_update {
                        lvl_seg = i32::from(seg.feature_data(segment_id, SegLvl::AltL));
                    } else {
                        lvl_seg += i32::from(seg.feature_data(segment_id, SegLvl::AltL));
                    }
                }

                let lvl_seg = lvl_seg.clamp(0, MAX_LOOP_FILTER as i32) as u8;

                if !lf.delta_enabled {
                    lvl_lookup = [[lvl_seg; MAX_MODE_LF_DELTAS]; MAX_REF_FRAMES]
                } else {
                    let intra_delta = lf.ref_deltas[INTRA_FRAME] as i32;
                    let mut intra_lvl = lvl_seg as i32 + (intra_delta << n_shift);

                    lvl_lookup = segmentation[segment_id as usize].lvl_lookup;
                    lvl_lookup[INTRA_FRAME][0] = intra_lvl.clamp(0, MAX_LOOP_FILTER as i32) as u8;

                    // Note, this array has the [0] element unspecified/unused in
                    // VP9. Confusing, but we do start to index from 1.
                    #[allow(clippy::needless_range_loop)]
                    for ref_ in LAST_FRAME..MAX_REF_FRAMES {
                        for mode in 0..MAX_MODE_LF_DELTAS {
                            let ref_delta = lf.ref_deltas[ref_] as i32;
                            let mode_delta = lf.mode_deltas[mode] as i32;

                            intra_lvl =
                                lvl_seg as i32 + (ref_delta << n_shift) + (mode_delta << n_shift);

                            lvl_lookup[ref_][mode] =
                                intra_lvl.clamp(0, MAX_LOOP_FILTER as i32) as u8;
                        }
                    }
                }
            }

            segmentation[usize::from(segment_id)] = Segmentation {
                lvl_lookup,
                luma_ac_quant_scale,
                luma_dc_quant_scale,
                chroma_ac_quant_scale,
                chroma_dc_quant_scale,
                reference_frame_enabled: seg.is_feature_enabled(segment_id, SegLvl::RefFrame),
                reference_frame: seg.feature_data(segment_id, SegLvl::RefFrame),
                reference_skip_enabled: seg.is_feature_enabled(segment_id, SegLvl::LvlSkip),
            }
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct FrameSize {
    width: u32,
    height: u32,
}

pub struct Frame<'a> {
    /// The bitstream data for this frame.
    bitstream: &'a [u8],
    /// The frame header.
    pub header: Header,
    /// The offset into T
    offset: usize,
    /// The size of the data in T
    size: usize,
}

impl<'a> Frame<'a> {
    pub fn new(bitstream: &'a [u8], header: Header, offset: usize, size: usize) -> Self {
        Self { bitstream, header, offset, size }
    }
}

impl<'a> AsRef<[u8]> for Frame<'a> {
    fn as_ref(&self) -> &[u8] {
        let data = self.bitstream;
        &data[self.offset..self.offset + self.size]
    }
}

/// A VP9 frame header.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Header {
    /// A subset of syntax, semantics and algorithms defined in a part.
    pub profile: Profile,
    /// The bit depth of the frame.
    pub bit_depth: BitDepth,
    /// Specifies the chroma subsampling format.
    pub subsampling_x: bool,
    /// Specifies the chroma subsampling format.
    pub subsampling_y: bool,
    /// Specifies the color space of the stream.
    pub color_space: ColorSpace,
    /// Specifies the black level and range of the luma and chroma signals as
    /// specified in Rec. ITU-R BT.709-6 and Rec. ITU-R BT.2020-2
    pub color_range: ColorRange,
    /// Indicates the frame indexed by frame_to_show_map_idx is to be displayed.
    /// If unset, indicates that further processing is required.
    pub show_existing_frame: bool,
    /// Specifies the frame to be displayed. It is only available if
    /// show_existing_frame is set.
    pub frame_to_show_map_idx: u8,
    /// Indicates whether a frame is a key frame.
    pub frame_type: FrameType,
    /// Whether this frame should be displayed.
    pub show_frame: bool,
    /// Whether error resilient mode is enabled.
    pub error_resilient_mode: bool,
    /// The width of the frame in pixels.
    pub width: u32,
    /// The height of the frame in pixels.
    pub height: u32,
    /// If unset, means that the render width and height are inferred from the
    /// frame width and height. If set, means that the render width and height
    /// are explicitly coded in the bitstream.
    pub render_and_frame_size_different: bool,
    /// The render width of the frame in pixels.
    pub render_width: u32,
    /// The render height of the frame in pixels.
    pub render_height: u32,
    /// If set, indicates that this frame is an intra-only frame. If unset,
    /// indicates that this frame is a inter frame.
    pub intra_only: bool,
    /// Specifies whether the frame context should be reset to default values.
    pub reset_frame_context: u8,
    /// Contains a bitmask that specifies which reference frame slots will be
    /// updated with the current frame after it is decoded.
    pub refresh_frame_flags: u8,
    /// Specifies which reference frames are used by inter frames. It is a
    /// requirement of bitstream conformance that the selected reference frames
    /// match the current frame in bit depth, profile, chroma subsampling, and
    /// color space.
    pub ref_frame_idx: [u8; REFS_PER_FRAME],
    /// Specifies the intended direction of the motion vector in time for each
    /// reference frame. A sign bias equal to 0 indicates that the reference
    /// frame is a backwards reference; a sign bias equal to 1 indicates that
    /// the reference frame is a forwards reference
    pub ref_frame_sign_bias: [u8; 4],
    /// If unset, specifies that motion vectors are specified to quarter pel
    /// precision. If set, specifies that motion vectors are specified to eighth
    /// pel precision.
    pub allow_high_precision_mv: bool,
    /// The interpolation filter parameters.
    pub interpolation_filter: InterpolationFilter,
    /// If set, indicates that the probabilities computed for this frame (after
    /// adapting to the observed frequencies if adaption is enabled) should be
    /// stored for reference by future frames. If unset, indicates that the
    /// probabilities should be discarded at the end of the frame.
    pub refresh_frame_context: bool,
    /// Whether parallel decoding mode is enabled.
    pub frame_parallel_decoding_mode: bool,
    /// Indicates the frame context to use.
    pub frame_context_idx: u8,
    /// The loop filter parameters
    pub lf: LoopFilterParams,
    /// The quantization parameters.
    pub quant: QuantizationParams,
    /// The segmentation parameters
    pub seg: SegmentationParams,
    /// Specifies the base 2 logarithm of the width of each tile (where the
    /// width is measured in units of 8x8 blocks). It is a requirement of
    /// bitstream conformance that tile_cols_log2 is less than or equal to 6.
    pub tile_cols_log2: u8,
    /// Specifies the base 2 logarithm of the height of each tile (where the
    /// height is measured in units of 8x8 blocks).
    pub tile_rows_log2: u8,
    /// Computed from the syntax elements. If set, indicates that the frame is
    /// coded using a special 4x4 transform designed for encoding frames that
    /// are bit-identical with the original frames.
    pub lossless: bool,
    /// Indicates the size of the compressed header in bytes.
    pub header_size_in_bytes: u16,
    /// Indicates the size of the uncompressed header in bytes.
    pub uncompressed_header_size_in_bytes: u16,
}

impl Header {
    /// An implementation of get_qindex as per "8.6.1 Dequantization functions"
    fn get_qindex(&self, segment_id: u8) -> u8 {
        let base_q_idx = self.quant.base_q_idx;

        if self.seg.is_feature_active(segment_id, SegLvl::AltQ) {
            let mut data = self.seg.feature_data(segment_id, SegLvl::AltQ) as i32;

            if !self.seg.abs_or_delta_update {
                data += base_q_idx as i32;
            }

            data.clamp(0, 255) as u8
        } else {
            base_q_idx
        }
    }

    /// An implementation of get_dc_quant as per "8.6.1 Dequantization functions"
    fn get_dc_quant(&self, segment_id: u8, luma: bool) -> i16 {
        let delta_q_dc =
            if luma { self.quant.delta_q_y_dc } else { self.quant.delta_q_uv_dc } as i32;
        let qindex = self.get_qindex(segment_id);
        let q_table_idx = (qindex as i32 + delta_q_dc).clamp(0, 255) as u8;

        let table = match self.bit_depth {
            BitDepth::Depth8 => &DC_QLOOKUP,
            BitDepth::Depth10 => &DC_QLOOKUP_10,
            BitDepth::Depth12 => &DC_QLOOKUP_12,
        };

        table[q_table_idx as usize]
    }

    /// An implementation of get_ac_quant as per "8.6.1 Dequantization functions"
    fn get_ac_quant(&self, segment_id: u8, luma: bool) -> i16 {
        let delta_q_ac = if luma { 0 } else { self.quant.delta_q_uv_ac } as i32;
        let qindex = self.get_qindex(segment_id);
        let q_table_idx = (qindex as i32 + delta_q_ac).clamp(0, 255) as u8;

        let table = match self.bit_depth {
            BitDepth::Depth8 => &AC_QLOOKUP,
            BitDepth::Depth10 => &AC_QLOOKUP_10,
            BitDepth::Depth12 => &AC_QLOOKUP_12,
        };

        table[q_table_idx as usize]
    }
}

/// The VP9 superframe header as per Annex B, B.2.1, B.2.2
struct SuperframeHeader {
    /// Indicates the number of frames within this superframe. NOTE - It is
    /// legal for a superframe to contain just a single frame and have NumFrames
    /// equal to 1.
    frames_in_superframe: u32,
    /// Specifies the size in bytes of frame number i (zero indexed) within this
    /// superframe.
    frame_sizes: Vec<usize>,
}

/// A VP9 bitstream parser.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Parser {
    bit_depth: BitDepth,
    subsampling_x: bool,
    subsampling_y: bool,
    color_space: ColorSpace,
    color_range: ColorRange,

    mi_cols: u32,
    mi_rows: u32,
    sb64_cols: u32,
    sb64_rows: u32,

    lf: LoopFilterParams,
    seg: SegmentationParams,

    reference_frame_sz: [FrameSize; REF_FRAMES],
}

impl Parser {
    fn parse_superframe_hdr(resource: impl AsRef<[u8]>) -> Result<SuperframeHeader, String> {
        let bitstream = resource.as_ref();

        // Skip to the end of the chunk.
        let mut reader = BitReader::new(&bitstream[bitstream.len() - 1..], false);

        // Try reading a superframe marker.
        let marker = reader.read_bits::<u32>(3)?;

        if marker != SUPERFRAME_MARKER {
            // Not a superframe
            return Ok(SuperframeHeader {
                frames_in_superframe: 1,
                frame_sizes: vec![bitstream.len()],
            });
        }

        let bytes_per_framesize = reader.read_bits::<u32>(2)? + 1;
        let frames_in_superframe = reader.read_bits::<u32>(3)? + 1;

        if frames_in_superframe > MAX_FRAMES_IN_SUPERFRAME as u32 {
            return Err(format!(
                "Broken stream: too many frames in superframe, expected a maximum of {:?}, found {:?}",
                MAX_FRAMES_IN_SUPERFRAME,
                frames_in_superframe
            ));
        }

        let sz_index = 2 + frames_in_superframe * bytes_per_framesize;

        let data = resource.as_ref();
        let index_offset = data.len() - sz_index as usize;
        let first_byte = data[index_offset];
        let last_byte = *data.last().ok_or_else(|| String::from("superframe header is empty"))?;

        if first_byte != last_byte {
            // Also not a superframe, we must pass both tests as per the specification.
            return Ok(SuperframeHeader {
                frames_in_superframe: 1,
                frame_sizes: vec![bitstream.len()],
            });
        }

        let mut frame_sizes = vec![];
        let mut reader = BitReader::new(&bitstream[index_offset..], false);

        // Skip the superframe header.
        let _ = reader.read_bits::<u32>(8)?;

        for _ in 0..frames_in_superframe {
            let mut frame_size = 0;

            for j in 0..bytes_per_framesize {
                frame_size |= reader.read_bits::<u32>(8)? << (j * 8);
            }

            frame_sizes.push(frame_size as usize);
        }

        Ok(SuperframeHeader { frames_in_superframe, frame_sizes })
    }

    fn read_signed_8(r: &mut BitReader, nbits: u8) -> Result<i8, String> {
        let value = r.read_bits::<u8>(nbits as usize)?;
        let negative = r.read_bit()?;

        if negative {
            Ok(-(value as i8))
        } else {
            Ok(value as i8)
        }
    }

    fn parse_frame_marker(r: &mut BitReader) -> Result<(), String> {
        let marker = r.read_bits::<u32>(2)?;

        if marker != FRAME_MARKER {
            return Err(format!("Broken stream: expected frame marker, found {:?}", marker));
        }

        Ok(())
    }

    fn parse_profile(r: &mut BitReader) -> Result<Profile, String> {
        let low = r.read_bits::<u32>(1)?;
        let high = r.read_bits::<u32>(1)?;

        let profile = (high << 1) | low;

        if profile == 3 {
            // Skip the reserved bit
            let _ = r.read_bit()?;
        }

        Profile::try_from(profile)
    }

    fn parse_frame_sync_code(r: &mut BitReader) -> Result<(), String> {
        let sync_code = r.read_bits::<u32>(24)?;

        if sync_code != SYNC_CODE {
            return Err(format!(
                "Broken stream: expected sync code == {:?}, found {:?}",
                SYNC_CODE, sync_code
            ));
        }

        Ok(())
    }

    fn parse_color_config(&mut self, r: &mut BitReader, hdr: &mut Header) -> Result<(), String> {
        if matches!(hdr.profile, Profile::Profile2 | Profile::Profile3) {
            let ten_or_twelve_bit = r.read_bit()?;
            if ten_or_twelve_bit {
                hdr.bit_depth = BitDepth::Depth12;
            } else {
                hdr.bit_depth = BitDepth::Depth10
            }
        } else {
            hdr.bit_depth = BitDepth::Depth8;
        }

        let color_space = r.read_bits::<u32>(3)?;
        hdr.color_space = ColorSpace::try_from(color_space)?;

        if !matches!(hdr.color_space, ColorSpace::CsSrgb) {
            let color_range = r.read_bits::<u32>(1)?;

            hdr.color_range = ColorRange::from(color_range != 0);

            if matches!(hdr.profile, Profile::Profile1 | Profile::Profile3) {
                hdr.subsampling_x = r.read_bit()?;
                hdr.subsampling_y = r.read_bit()?;

                // Skip the reserved bit
                let _ = r.read_bit()?;
            } else {
                hdr.subsampling_x = true;
                hdr.subsampling_y = true;
            }
        } else {
            hdr.color_range = ColorRange::Full;
            if matches!(hdr.profile, Profile::Profile1 | Profile::Profile3) {
                hdr.subsampling_x = false;
                hdr.subsampling_y = false;

                // Skip the reserved bit
                let _ = r.read_bit()?;
            }
        }

        self.bit_depth = hdr.bit_depth;
        self.color_space = hdr.color_space;
        self.subsampling_x = hdr.subsampling_x;
        self.subsampling_y = hdr.subsampling_y;
        self.color_range = hdr.color_range;

        Ok(())
    }

    fn compute_image_size(&mut self, width: u32, height: u32) {
        self.mi_cols = (width + 7) >> 3;
        self.mi_rows = (height + 7) >> 3;
        self.sb64_cols = (self.mi_cols + 7) >> 3;
        self.sb64_rows = (self.mi_rows + 7) >> 3;
    }

    fn parse_frame_size(&mut self, r: &mut BitReader, hdr: &mut Header) -> Result<(), String> {
        hdr.width = r.read_bits::<u32>(16)? + 1;
        hdr.height = r.read_bits::<u32>(16)? + 1;
        self.compute_image_size(hdr.width, hdr.height);
        Ok(())
    }

    fn parse_render_size(r: &mut BitReader, hdr: &mut Header) -> Result<(), String> {
        hdr.render_and_frame_size_different = r.read_bit()?;
        if hdr.render_and_frame_size_different {
            hdr.render_width = r.read_bits::<u32>(16)? + 1;
            hdr.render_height = r.read_bits::<u32>(16)? + 1;
        } else {
            hdr.render_width = hdr.width;
            hdr.render_height = hdr.height;
        }

        Ok(())
    }

    fn parse_frame_size_with_refs(
        &mut self,
        r: &mut BitReader,
        hdr: &mut Header,
    ) -> Result<(), String> {
        let mut found_ref = false;

        for i in 0..REFS_PER_FRAME {
            found_ref = r.read_bit()?;

            if found_ref {
                let idx = hdr.ref_frame_idx[i] as usize;
                hdr.width = self.reference_frame_sz[idx].width;
                hdr.height = self.reference_frame_sz[idx].height;
                break;
            }
        }

        if !found_ref {
            self.parse_frame_size(r, hdr)?;
        } else {
            self.compute_image_size(hdr.width, hdr.height)
        }

        Self::parse_render_size(r, hdr)
    }

    fn read_interpolation_filter(r: &mut BitReader) -> Result<InterpolationFilter, String> {
        const LITERAL_TO_TYPE: [InterpolationFilter; 4] = [
            InterpolationFilter::EightTapSmooth,
            InterpolationFilter::EightTap,
            InterpolationFilter::EightTapSharp,
            InterpolationFilter::Bilinear,
        ];

        let is_filter_switchable = r.read_bit()?;

        Ok(if is_filter_switchable {
            InterpolationFilter::Switchable
        } else {
            let raw_interpolation_filter = r.read_bits::<u32>(2)?;
            LITERAL_TO_TYPE[raw_interpolation_filter as usize]
        })
    }

    fn setup_past_independence(&mut self, hdr: &mut Header) {
        self.seg.feature_enabled = Default::default();
        self.seg.feature_data = Default::default();
        self.seg.abs_or_delta_update = false;

        self.lf.delta_enabled = true;
        self.lf.ref_deltas[ReferenceFrameType::Intra as usize] = 1;
        self.lf.ref_deltas[ReferenceFrameType::Last as usize] = 0;
        self.lf.ref_deltas[ReferenceFrameType::Golden as usize] = -1;
        self.lf.ref_deltas[ReferenceFrameType::AltRef as usize] = -1;

        self.lf.mode_deltas = Default::default();
        hdr.ref_frame_sign_bias = Default::default();
    }

    fn parse_loop_filter_params(
        r: &mut BitReader,
        lf: &mut LoopFilterParams,
    ) -> Result<(), String> {
        lf.level = r.read_bits::<u8>(6)?;
        lf.sharpness = r.read_bits::<u8>(3)?;
        lf.delta_enabled = r.read_bit()?;

        if lf.delta_enabled {
            lf.delta_update = r.read_bit()?;
            if lf.delta_update {
                for i in 0..MAX_REF_LF_DELTAS {
                    lf.update_ref_delta[i] = r.read_bit()?;
                    if lf.update_ref_delta[i] {
                        lf.ref_deltas[i] = Self::read_signed_8(r, 6)?;
                    }
                }

                for i in 0..MAX_MODE_LF_DELTAS {
                    lf.update_mode_delta[i] = r.read_bit()?;
                    if lf.update_mode_delta[i] {
                        lf.mode_deltas[i] = Self::read_signed_8(r, 6)?;
                    }
                }
            }
        }

        Ok(())
    }

    fn read_delta_q(r: &mut BitReader, value: &mut i8) -> Result<(), String> {
        let delta_coded = r.read_bit()?;

        if delta_coded {
            *value = Self::read_signed_8(r, 4)?;
        } else {
            *value = 0;
        }

        Ok(())
    }

    fn parse_quantization_params(r: &mut BitReader, hdr: &mut Header) -> Result<(), String> {
        let quant = &mut hdr.quant;

        quant.base_q_idx = r.read_bits::<u8>(8)?;

        Self::read_delta_q(r, &mut quant.delta_q_y_dc)?;
        Self::read_delta_q(r, &mut quant.delta_q_uv_dc)?;
        Self::read_delta_q(r, &mut quant.delta_q_uv_ac)?;

        hdr.lossless = quant.base_q_idx == 0
            && quant.delta_q_y_dc == 0
            && quant.delta_q_uv_dc == 0
            && quant.delta_q_uv_ac == 0;

        Ok(())
    }

    fn read_prob(r: &mut BitReader) -> Result<u8, String> {
        let prob_coded = r.read_bit()?;

        let prob = if prob_coded { r.read_bits::<u8>(8)? } else { 255 };

        Ok(prob)
    }

    fn parse_segmentation_params(
        r: &mut BitReader,
        seg: &mut SegmentationParams,
    ) -> Result<(), String> {
        const SEGMENTATION_FEATURE_BITS: [u8; SEG_LVL_MAX] = [8, 6, 2, 0];
        const SEGMENTATION_FEATURE_SIGNED: [bool; SEG_LVL_MAX] = [true, true, false, false];

        seg.update_map = false;
        seg.update_data = false;

        seg.enabled = r.read_bit()?;

        if !seg.enabled {
            return Ok(());
        }

        seg.update_map = r.read_bit()?;

        if seg.update_map {
            for i in 0..SEG_TREE_PROBS {
                seg.tree_probs[i] = Self::read_prob(r)?;
            }

            seg.temporal_update = r.read_bit()?;

            for i in 0..PREDICTION_PROBS {
                seg.pred_probs[i] = if seg.temporal_update { Self::read_prob(r)? } else { 255 };
            }
        }

        seg.update_data = r.read_bit()?;

        if seg.update_data {
            seg.abs_or_delta_update = r.read_bit()?;
            for i in 0..MAX_SEGMENTS {
                for j in 0..SEG_LVL_MAX {
                    seg.feature_enabled[i][j] = r.read_bit()?;
                    if seg.feature_enabled[i][j] {
                        let bits_to_read = SEGMENTATION_FEATURE_BITS[j];
                        let mut feature_value = r.read_bits_signed::<i16>(bits_to_read as usize)?;

                        if SEGMENTATION_FEATURE_SIGNED[j] {
                            let feature_sign = r.read_bit()?;

                            if feature_sign {
                                feature_value = -feature_value;
                            }
                        }

                        seg.feature_data[i][j] = feature_value;
                    }
                }
            }
        }

        Ok(())
    }

    fn calc_min_log2_tile_cols(sb64_cols: u32) -> u8 {
        let mut min_log2 = 0;

        while (MAX_TILE_WIDTH_B64 << min_log2) < sb64_cols {
            min_log2 += 1;
        }

        min_log2
    }

    fn calc_max_log2_tile_cols(sb64_cols: u32) -> u8 {
        let mut max_log2 = 1;

        while (sb64_cols >> max_log2) >= MIN_TILE_WIDTH_B64 {
            max_log2 += 1;
        }

        max_log2 - 1
    }

    fn parse_tile_info(&self, r: &mut BitReader, hdr: &mut Header) -> Result<(), String> {
        let max_log2_tile_cols = Self::calc_max_log2_tile_cols(self.sb64_cols);

        hdr.tile_cols_log2 = Self::calc_min_log2_tile_cols(self.sb64_cols);

        while hdr.tile_cols_log2 < max_log2_tile_cols {
            let increment_tile_cols_log2 = r.read_bit()?;

            if increment_tile_cols_log2 {
                hdr.tile_cols_log2 += 1;
            } else {
                break;
            }
        }

        hdr.tile_rows_log2 = r.read_bits::<u8>(1)?;

        if hdr.tile_rows_log2 > 0 {
            let increment_tile_rows_log2 = r.read_bit()?;
            hdr.tile_rows_log2 += increment_tile_rows_log2 as u8;
        }

        Ok(())
    }

    fn parse_frame_header(
        &mut self,
        resource: impl AsRef<[u8]>,
        offset: usize,
    ) -> Result<Header, String> {
        let data = &resource.as_ref()[offset..];
        let mut r = BitReader::new(data, false);
        let mut hdr = Header::default();

        Self::parse_frame_marker(&mut r)?;
        hdr.profile = Self::parse_profile(&mut r)?;

        hdr.show_existing_frame = r.read_bit()?;

        if hdr.show_existing_frame {
            hdr.frame_to_show_map_idx = r.read_bits::<u8>(3)?;
            return Ok(hdr);
        }

        hdr.frame_type = FrameType::try_from(r.read_bits::<u8>(1)?)?;

        hdr.show_frame = r.read_bit()?;
        hdr.error_resilient_mode = r.read_bit()?;

        let frame_is_intra;

        if matches!(hdr.frame_type, FrameType::KeyFrame) {
            Self::parse_frame_sync_code(&mut r)?;
            self.parse_color_config(&mut r, &mut hdr)?;
            self.parse_frame_size(&mut r, &mut hdr)?;
            Self::parse_render_size(&mut r, &mut hdr)?;
            hdr.refresh_frame_flags = 0xff;
            frame_is_intra = true;
        } else {
            if !hdr.show_frame {
                hdr.intra_only = r.read_bit()?;
            }

            frame_is_intra = hdr.intra_only;

            if !hdr.error_resilient_mode {
                hdr.reset_frame_context = r.read_bits::<u8>(2)?;
            } else {
                hdr.reset_frame_context = 0;
            }

            if hdr.intra_only {
                Self::parse_frame_sync_code(&mut r)?;

                if !matches!(hdr.profile, Profile::Profile0) {
                    self.parse_color_config(&mut r, &mut hdr)?;
                } else {
                    hdr.color_space = ColorSpace::Bt601;
                    hdr.subsampling_x = true;
                    hdr.subsampling_y = true;
                    hdr.bit_depth = BitDepth::Depth8;

                    self.color_space = hdr.color_space;
                    self.subsampling_x = hdr.subsampling_x;
                    self.subsampling_y = hdr.subsampling_y;
                    self.bit_depth = hdr.bit_depth;
                }

                hdr.refresh_frame_flags = r.read_bits::<u8>(8)?;
                self.parse_frame_size(&mut r, &mut hdr)?;
                Self::parse_render_size(&mut r, &mut hdr)?;
            } else {
                // Copy from our cached version
                hdr.color_space = self.color_space;
                hdr.color_range = self.color_range;
                hdr.subsampling_x = self.subsampling_x;
                hdr.subsampling_y = self.subsampling_y;
                hdr.bit_depth = self.bit_depth;

                hdr.refresh_frame_flags = r.read_bits::<u8>(8)?;

                for i in 0..REFS_PER_FRAME {
                    hdr.ref_frame_idx[i] = r.read_bits::<u8>(3)?;
                    hdr.ref_frame_sign_bias[ReferenceFrameType::Last as usize + i] =
                        r.read_bits::<u8>(1)?;
                }

                self.parse_frame_size_with_refs(&mut r, &mut hdr)?;
                hdr.allow_high_precision_mv = r.read_bit()?;
                hdr.interpolation_filter = Self::read_interpolation_filter(&mut r)?;
            }
        }

        if !hdr.error_resilient_mode {
            hdr.refresh_frame_context = r.read_bit()?;
            hdr.frame_parallel_decoding_mode = r.read_bit()?;
        } else {
            hdr.refresh_frame_context = false;
            hdr.frame_parallel_decoding_mode = true;
        }

        hdr.frame_context_idx = r.read_bits::<u8>(2)?;

        if frame_is_intra || hdr.error_resilient_mode {
            self.setup_past_independence(&mut hdr);
        }

        Self::parse_loop_filter_params(&mut r, &mut self.lf)?;
        Self::parse_quantization_params(&mut r, &mut hdr)?;
        Self::parse_segmentation_params(&mut r, &mut self.seg)?;
        self.parse_tile_info(&mut r, &mut hdr)?;

        hdr.header_size_in_bytes = r.read_bits::<u16>(16)?;

        hdr.lf = self.lf.clone();
        hdr.seg = self.seg.clone();

        for i in 0..REF_FRAMES {
            let flag = 1 << i;
            if hdr.refresh_frame_flags & flag != 0 {
                self.reference_frame_sz[i].width = hdr.width;
                self.reference_frame_sz[i].height = hdr.height;
            }
        }

        hdr.uncompressed_header_size_in_bytes = (r.position() as u16 + 7) / 8;

        Ok(hdr)
    }

    /// Parse a single VP9 frame.
    pub fn parse_frame<'a>(
        &mut self,
        bitstream: &'a [u8],
        offset: usize,
        size: usize,
    ) -> Result<Frame<'a>, String> {
        let header = self.parse_frame_header(bitstream, offset)?;

        Ok(Frame { header, bitstream, offset, size })
    }

    /// Parses VP9 frames from the data in `resource`. This can result in more than one frame if the
    /// data passed in contains a VP9 superframe.
    pub fn parse_chunk<'a>(&mut self, resource: &'a [u8]) -> Result<Vec<Frame<'a>>, String> {
        let superframe_hdr = Parser::parse_superframe_hdr(resource)?;
        let mut offset = 0;

        let mut frames = vec![];

        for i in 0..superframe_hdr.frames_in_superframe {
            let frame_sz = superframe_hdr.frame_sizes[i as usize];
            let frame = self.parse_frame(resource, offset, frame_sz)?;
            offset += frame_sz;
            frames.push(frame);
        }

        Ok(frames)
    }
}

#[cfg(test)]
mod tests {
    use crate::bitstream_utils::IvfIterator;
    use crate::codec::vp9::parser::BitDepth;
    use crate::codec::vp9::parser::ColorSpace;
    use crate::codec::vp9::parser::FrameType;
    use crate::codec::vp9::parser::InterpolationFilter;
    use crate::codec::vp9::parser::Parser;
    use crate::codec::vp9::parser::Profile;
    use crate::codec::vp9::parser::MAX_SEGMENTS;
    use crate::codec::vp9::parser::SEG_LVL_MAX;

    #[test]
    fn test_parse_superframe() {
        // Demuxed, raw vp9 superframe
        const VP9_TEST_SUPERFRAME: &[u8] = include_bytes!("test_data/vp9-superframe.bin");

        let mut parser = Parser::default();
        let frames = parser.parse_chunk(VP9_TEST_SUPERFRAME).expect("Parsing a superframe failed");

        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].offset, 0);
        assert_eq!(frames[0].size, 1333);
        assert_eq!(frames[1].offset, 1333);
        assert_eq!(frames[1].size, 214);
    }

    #[test]
    fn test_parse_test25fps() {
        // Muxed as IVF
        const TEST_STREAM: &[u8] = include_bytes!("test_data/test-25fps.vp9");

        let mut parser = Parser::default();
        let ivf_iter = IvfIterator::new(TEST_STREAM).expect("Failed to construct IVF iterator");

        for (frame_n, packet) in ivf_iter.enumerate() {
            let frames = parser.parse_chunk(packet.as_ref()).expect("Parsing a superframe failed");

            if frame_n == 0 {
                assert_eq!(frames.len(), 1);
                let h = &frames[0].header;

                assert!(matches!(h.profile, Profile::Profile0));
                assert!(matches!(h.bit_depth, BitDepth::Depth8));

                assert!(h.subsampling_x);
                assert!(h.subsampling_y);

                assert!(matches!(h.color_space, ColorSpace::Unknown));
                assert!(matches!(h.color_range, crate::ColorRange::Limited));

                assert!(!h.show_existing_frame);
                assert_eq!(h.frame_to_show_map_idx, 0);

                assert!(matches!(h.frame_type, FrameType::KeyFrame));
                assert!(h.show_frame);
                assert!(!h.error_resilient_mode);

                assert_eq!(h.width, 320);
                assert_eq!(h.height, 240);

                assert!(!h.render_and_frame_size_different);

                assert_eq!(h.render_width, 320);
                assert_eq!(h.render_height, 240);

                assert!(!h.intra_only);
                assert_eq!(h.reset_frame_context, 0);

                assert_eq!(h.refresh_frame_flags, 0xff);
                assert_eq!(h.ref_frame_idx, [0, 0, 0]);
                assert_eq!(h.ref_frame_sign_bias, [0, 0, 0, 0]);

                assert!(!h.allow_high_precision_mv);
                assert!(matches!(h.interpolation_filter, InterpolationFilter::EightTap));

                assert!(h.refresh_frame_context);
                assert!(h.frame_parallel_decoding_mode);
                assert_eq!(h.frame_context_idx, 0);

                let lf = &h.lf;
                assert_eq!(lf.level, 9);
                assert_eq!(lf.sharpness, 0);

                assert!(lf.delta_enabled);
                assert!(lf.delta_update);

                assert_eq!(lf.update_ref_delta, [true, false, true, true]);
                assert_eq!(lf.ref_deltas, [1, 0, -1, -1]);

                assert_eq!(lf.update_mode_delta, [false, false]);

                let q = &h.quant;

                assert_eq!(q.base_q_idx, 65);
                assert_eq!(q.delta_q_y_dc, 0);
                assert_eq!(q.delta_q_uv_dc, 0);
                assert_eq!(q.delta_q_uv_ac, 0);

                let s = &h.seg;

                assert!(!s.enabled);
                assert!(!s.update_map);
                assert_eq!(s.tree_probs, [0, 0, 0, 0, 0, 0, 0]);
                assert_eq!(s.pred_probs, [0, 0, 0]);
                assert!(!s.temporal_update);
                assert!(!s.update_data);
                assert!(!s.abs_or_delta_update);
                assert_eq!(s.feature_enabled, [[false; SEG_LVL_MAX]; MAX_SEGMENTS]);
                assert_eq!(s.feature_data, [[0; SEG_LVL_MAX]; MAX_SEGMENTS]);

                assert_eq!(h.tile_cols_log2, 0);
                assert_eq!(h.tile_rows_log2, 0);
                assert_eq!(h.header_size_in_bytes, 120);

                assert!(!h.lossless);
            } else if frame_n == 1 {
                assert_eq!(frames.len(), 2);

                assert_eq!(frames[0].offset, 0);
                assert_eq!(frames[0].size, 2390);
                assert_eq!(frames[1].offset, 2390);
                assert_eq!(frames[1].size, 108);

                let h = &frames[0].header;

                assert!(matches!(h.profile, Profile::Profile0));
                assert!(matches!(h.bit_depth, BitDepth::Depth8));

                assert!(h.subsampling_x);
                assert!(h.subsampling_y);

                assert!(matches!(h.color_space, ColorSpace::Unknown));
                assert!(matches!(h.color_range, crate::ColorRange::Limited));

                assert!(!h.show_existing_frame);
                assert_eq!(h.frame_to_show_map_idx, 0);

                assert!(matches!(h.frame_type, FrameType::InterFrame));
                assert!(!h.show_frame);
                assert!(!h.error_resilient_mode);

                assert_eq!(h.width, 320);
                assert_eq!(h.height, 240);

                assert!(!h.render_and_frame_size_different);

                assert_eq!(h.render_width, 320);
                assert_eq!(h.render_height, 240);

                assert!(!h.intra_only);
                assert_eq!(h.reset_frame_context, 0);

                assert_eq!(h.refresh_frame_flags, 4);
                assert_eq!(h.ref_frame_idx, [0, 1, 2]);
                assert_eq!(h.ref_frame_sign_bias, [0, 0, 0, 0]);

                assert!(h.allow_high_precision_mv);
                assert!(matches!(h.interpolation_filter, InterpolationFilter::EightTap));

                assert!(h.refresh_frame_context);
                assert!(h.frame_parallel_decoding_mode);
                assert_eq!(h.frame_context_idx, 1);

                let lf = &h.lf;
                assert_eq!(lf.level, 15);
                assert_eq!(lf.sharpness, 0);

                assert!(lf.delta_enabled);
                assert!(!lf.delta_update);

                assert_eq!(lf.update_ref_delta, [true, false, true, true]);
                assert_eq!(lf.ref_deltas, [1, 0, -1, -1]);

                assert_eq!(lf.update_mode_delta, [false, false]);

                let q = &h.quant;

                assert_eq!(q.base_q_idx, 112);
                assert_eq!(q.delta_q_y_dc, 0);
                assert_eq!(q.delta_q_uv_dc, 0);
                assert_eq!(q.delta_q_uv_ac, 0);

                let s = &h.seg;

                assert!(!s.enabled);
                assert!(!s.update_map);
                assert_eq!(s.tree_probs, [0, 0, 0, 0, 0, 0, 0]);
                assert_eq!(s.pred_probs, [0, 0, 0]);
                assert!(!s.temporal_update);
                assert!(!s.update_data);
                assert!(!s.abs_or_delta_update);
                assert_eq!(s.feature_enabled, [[false; SEG_LVL_MAX]; MAX_SEGMENTS]);
                assert_eq!(s.feature_data, [[0; SEG_LVL_MAX]; MAX_SEGMENTS]);

                assert_eq!(h.tile_cols_log2, 0);
                assert_eq!(h.tile_rows_log2, 0);
                assert_eq!(h.header_size_in_bytes, 48);

                assert!(!h.lossless);

                let h = &frames[1].header;

                assert!(matches!(h.profile, Profile::Profile0));
                assert!(matches!(h.bit_depth, BitDepth::Depth8));

                assert!(h.subsampling_x);
                assert!(h.subsampling_y);

                assert!(matches!(h.color_space, ColorSpace::Unknown));
                assert!(matches!(h.color_range, crate::ColorRange::Limited));

                assert!(!h.show_existing_frame);
                assert_eq!(h.frame_to_show_map_idx, 0);

                assert!(matches!(h.frame_type, FrameType::InterFrame));
                assert!(h.show_frame);
                assert!(!h.error_resilient_mode);

                assert_eq!(h.width, 320);
                assert_eq!(h.height, 240);

                assert!(!h.render_and_frame_size_different);

                assert_eq!(h.render_width, 320);
                assert_eq!(h.render_height, 240);

                assert!(!h.intra_only);
                assert_eq!(h.reset_frame_context, 0);

                assert_eq!(h.refresh_frame_flags, 1);
                assert_eq!(h.ref_frame_idx, [0, 1, 2]);
                assert_eq!(h.ref_frame_sign_bias, [0, 0, 0, 1]);

                assert!(!h.allow_high_precision_mv);
                assert!(matches!(h.interpolation_filter, InterpolationFilter::EightTap));

                assert!(h.refresh_frame_context);
                assert!(h.frame_parallel_decoding_mode);
                assert_eq!(h.frame_context_idx, 0);

                let lf = &h.lf;
                assert_eq!(lf.level, 36);
                assert_eq!(lf.sharpness, 0);

                assert!(lf.delta_enabled);
                assert!(!lf.delta_update);

                assert_eq!(lf.update_ref_delta, [true, false, true, true]);
                assert_eq!(lf.ref_deltas, [1, 0, -1, -1]);

                assert_eq!(lf.update_mode_delta, [false, false]);

                let q = &h.quant;

                assert_eq!(q.base_q_idx, 216);
                assert_eq!(q.delta_q_y_dc, 0);
                assert_eq!(q.delta_q_uv_dc, 0);
                assert_eq!(q.delta_q_uv_ac, 0);

                let s = &h.seg;

                assert!(!s.enabled);
                assert!(!s.update_map);
                assert_eq!(s.tree_probs, [0, 0, 0, 0, 0, 0, 0]);
                assert_eq!(s.pred_probs, [0, 0, 0]);
                assert!(!s.temporal_update);
                assert!(!s.update_data);
                assert!(!s.abs_or_delta_update);
                assert_eq!(s.feature_enabled, [[false; SEG_LVL_MAX]; MAX_SEGMENTS]);
                assert_eq!(s.feature_data, [[0; SEG_LVL_MAX]; MAX_SEGMENTS]);

                assert_eq!(h.tile_cols_log2, 0);
                assert_eq!(h.tile_rows_log2, 0);
                assert_eq!(h.header_size_in_bytes, 9);

                assert!(!h.lossless);
            }
        }
    }
}
