// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides tools to help decode and encode various video codecs, leveraging the
//! hardware acceleration available on the target.
//!
//! The [codec] module contains tools to parse encoded video streams like H.264 or VP9 and extract
//! the information useful in order to perform e.g. hardware-accelerated decoding.
//!
//! The [backend] module contains common backend code. A backend is a provider of some way to
//! decode or encode a particular codec, like VAAPI.
//!
//! The [decoder] module contains decoders that can turn an encoded video stream into a sequence of
//! decoded frames using the hardware acceleration available on the host.
//!
//! The [encoder] module contains encoder that can turn a picture sequence into a compressed
//! sequence of decodable encoded packets using the hardware acceleration available on the host.
//!
//! The [utils] module contains some useful code that is shared between different parts of this
//! crate and didn't fit any of the modules above.

pub mod bitstream_utils;
pub mod codec;

#[cfg(feature = "backend")]
pub mod backend;
#[cfg(feature = "ubc")]
pub mod bitrate_ctrl;
#[cfg(feature = "backend")]
pub mod c2_wrapper;
#[cfg(feature = "backend")]
pub mod capabilities;
#[cfg(feature = "backend")]
pub mod decoder;
#[cfg(feature = "v4l2")]
pub mod device;
#[cfg(feature = "backend")]
pub mod encoder;
#[cfg(feature = "backend")]
pub mod image_processing;
#[cfg(feature = "backend")]
pub mod utils;
#[cfg(feature = "backend")]
pub mod video_frame;

use std::str::FromStr;

#[cfg(feature = "vaapi")]
pub use libva;
#[cfg(feature = "v4l2")]
pub use v4l2r;

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum FrameMemoryType {
    Managed,
    Prime,
    User,
}

impl FromStr for FrameMemoryType {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "managed" => Ok(FrameMemoryType::Managed),
            "prime" => Ok(FrameMemoryType::Prime),
            "user" => Ok(FrameMemoryType::User),
            _ => Err("unrecognized memory type. Valid values: managed, prime, user"),
        }
    }
}

/// Rounding modes for `Resolution`
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ResolutionRoundMode {
    /// Rounds component-wise to the next even value.
    Even,
}

/// A frame resolution in pixels.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct Resolution {
    pub width: u32,
    pub height: u32,
}

impl Resolution {
    /// Whether `self` can contain `other`.
    pub fn can_contain(&self, other: Self) -> bool {
        self.width >= other.width && self.height >= other.height
    }

    /// Rounds `self` according to `rnd_mode`.
    pub fn round(mut self, rnd_mode: ResolutionRoundMode) -> Self {
        match rnd_mode {
            ResolutionRoundMode::Even => {
                if self.width % 2 != 0 {
                    self.width += 1;
                }

                if self.height % 2 != 0 {
                    self.height += 1;
                }
            }
        }

        self
    }

    pub fn get_area(&self) -> usize {
        (self.width as usize) * (self.height as usize)
    }
}

impl From<(u32, u32)> for Resolution {
    fn from(value: (u32, u32)) -> Self {
        Self { width: value.0, height: value.1 }
    }
}

impl From<Resolution> for (u32, u32) {
    fn from(value: Resolution) -> Self {
        (value.width, value.height)
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

impl From<Rect> for Resolution {
    fn from(value: Rect) -> Self {
        Self { width: value.width - value.x, height: value.height - value.y }
    }
}

impl From<Resolution> for Rect {
    fn from(value: Resolution) -> Self {
        Self { x: 0, y: 0, width: value.width, height: value.height }
    }
}

impl From<((u32, u32), (u32, u32))> for Rect {
    fn from(value: ((u32, u32), (u32, u32))) -> Self {
        Self { x: value.0 .0, y: value.0 .1, width: value.1 .0, height: value.1 .1 }
    }
}
/// Wrapper around u32 when they are meant to be a fourcc.
///
/// Provides conversion and display/debug implementations useful when dealing with fourcc codes.
#[derive(Clone, Copy, Default, PartialEq)]
pub struct Fourcc(u32);

impl From<u32> for Fourcc {
    fn from(fourcc: u32) -> Self {
        Self(fourcc)
    }
}

impl From<Fourcc> for u32 {
    fn from(fourcc: Fourcc) -> Self {
        fourcc.0
    }
}

impl From<&[u8; 4]> for Fourcc {
    fn from(n: &[u8; 4]) -> Self {
        Self(n[0] as u32 | (n[1] as u32) << 8 | (n[2] as u32) << 16 | (n[3] as u32) << 24)
    }
}

impl From<Fourcc> for [u8; 4] {
    fn from(n: Fourcc) -> Self {
        [n.0 as u8, (n.0 >> 8) as u8, (n.0 >> 16) as u8, (n.0 >> 24) as u8]
    }
}

impl std::fmt::Display for Fourcc {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let c: [u8; 4] = (*self).into();

        f.write_fmt(format_args!(
            "{}{}{}{}",
            c[0] as char, c[1] as char, c[2] as char, c[3] as char
        ))
    }
}

impl std::fmt::Debug for Fourcc {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!("0x{:08x} ({})", self.0, self))
    }
}

// Translate fourcc to v4l2 fourcc value.
pub fn fourcc_for_v4l2_stateless(fourcc: Fourcc) -> Result<Fourcc, String> {
    match fourcc.to_string().as_str() {
        "VP80" => Ok(Fourcc::from(b"VP8F")),
        "VP90" => Ok(Fourcc::from(b"VP9F")),
        "H264" => Ok(Fourcc::from(b"S264")),
        "H265" => Ok(Fourcc::from(b"S265")),
        "AV1F" => Ok(Fourcc::from(b"AV1F")),
        _ => Err("Unsupported fourcc".into()),
    }
}

/// Formats that buffers can be mapped into for the CPU to read.
///
/// The conventions here largely follow these of libyuv.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub enum DecodedFormat {
    /// ARGB interleaved, 8 bits per sample.
    AR24,
    /// Y, U and V planes, 4:2:0 sampling, 8 bits per sample.
    I420,
    /// One Y and one interleaved UV plane, 4:2:0 sampling, 8 bits per sample.
    NV12,
    /// Y, U and V planes, 4:2:2 sampling, 8 bits per sample.
    I422,
    /// Y, U and V planes, 4:4:4 sampling, 8 bits per sample.
    I444,
    /// Y, U and V planes, 4:2:0 sampling, 16 bits per sample, LE. Only the 10 LSBs are used.
    I010,
    /// Y, U and V planes, 4:2:0 sampling, 16 bits per sample, LE. Only the 12 LSBs are used.
    I012,
    /// Y, U and V planes, 4:2:2 sampling, 16 bits per sample, LE. Only the 10 LSBs are used.
    I210,
    /// Y, U and V planes, 4:2:2 sampling, 16 bits per sample, LE. Only the 12 LSBs are used.
    I212,
    /// Y, U and V planes, 4:4:4 sampling, 16 bits per sample, LE. Only the 10 LSBs are used.
    I410,
    /// Y, U and V planes, 4:4:4 sampling, 16 bits per sample, LE. Only the 12 LSBs are used.
    I412,
    /// One Y and one interleaved UV plane, 4:2:0 sampling, 8 bits per sample.
    /// In a tiled format.
    MM21,
    /// One Y and one interleaved UV plane, 4:2:0 sampling, 10 bits per sample.
    /// In a tiled format.
    MT2T,
    // Y, U, and V planes, 4:2:0 sampling, 10 bits per sample.
    P010,
    /// Y, V and U planes, 4:2:0 sampling, 8 bits per sample.
    /// Like `I420` with U and V planes swapped.
    YV12,
}

impl FromStr for DecodedFormat {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "ar24" | "AR24" => Ok(DecodedFormat::AR24),
            "i420" | "I420" => Ok(DecodedFormat::I420),
            "i422" | "I422" => Ok(DecodedFormat::I422),
            "i444" | "I444" => Ok(DecodedFormat::I444),
            "nv12" | "NV12" => Ok(DecodedFormat::NV12),
            "i010" | "I010" => Ok(DecodedFormat::I010),
            "i012" | "I012" => Ok(DecodedFormat::I012),
            "i210" | "I210" => Ok(DecodedFormat::I210),
            "i212" | "I212" => Ok(DecodedFormat::I212),
            "i410" | "I410" => Ok(DecodedFormat::I410),
            "i412" | "I412" => Ok(DecodedFormat::I412),
            "mm21" | "MM21" => Ok(DecodedFormat::MM21),
            "mt2t" | "MT2T" => Ok(DecodedFormat::MT2T),
            "p010" | "P010" => Ok(DecodedFormat::P010),
            "yv12" | "YV12" => Ok(DecodedFormat::YV12),
            _ => Err("unrecognized output format. \
                Valid values: ar24, i420, nv12, i422, i444, i010, i012, i210, i212, i410, i412, \
                mm21, mt2t, p010, yv12"),
        }
    }
}

impl From<Fourcc> for DecodedFormat {
    fn from(fourcc: Fourcc) -> DecodedFormat {
        match fourcc.to_string().as_str() {
            "AR24" => DecodedFormat::AR24,
            "I420" => DecodedFormat::I420,
            "NV12" | "NM12" => DecodedFormat::NV12,
            "MM21" => DecodedFormat::MM21,
            "MT2T" => DecodedFormat::MT2T,
            "P010" => DecodedFormat::P010,
            "YV12" => DecodedFormat::YV12,
            _ => todo!("Fourcc {} not yet supported", fourcc),
        }
    }
}

impl From<DecodedFormat> for Fourcc {
    fn from(format: DecodedFormat) -> Fourcc {
        match format {
            DecodedFormat::AR24 => Fourcc::from(b"AR24"),
            DecodedFormat::I420 => Fourcc::from(b"I420"),
            DecodedFormat::NV12 => Fourcc::from(b"NV12"),
            DecodedFormat::MM21 => Fourcc::from(b"MM21"),
            DecodedFormat::MT2T => Fourcc::from(b"MT2T"),
            DecodedFormat::P010 => Fourcc::from(b"P010"),
            DecodedFormat::YV12 => Fourcc::from(b"YV12"),
            _ => todo!(),
        }
    }
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum EncodedFormat {
    H264,
    H265,
    VP8,
    VP9,
    AV1,
}

impl FromStr for EncodedFormat {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "h264" | "H264" => Ok(EncodedFormat::H264),
            "h265" | "H265" => Ok(EncodedFormat::H265),
            "vp8" | "VP8" => Ok(EncodedFormat::VP8),
            "vp9" | "VP9" => Ok(EncodedFormat::VP9),
            "av1" | "AV1" => Ok(EncodedFormat::AV1),
            _ => Err("unrecognized input format. Valid values: h264, h265, vp8, vp9, av1"),
        }
    }
}

impl From<Fourcc> for EncodedFormat {
    fn from(fourcc: Fourcc) -> EncodedFormat {
        match fourcc.to_string().as_str() {
            "H264" => EncodedFormat::H264,
            "H265" => EncodedFormat::H265,
            "VP80" => EncodedFormat::VP8,
            "VP90" => EncodedFormat::VP9,
            "AV1F" => EncodedFormat::AV1,
            _ => todo!("Fourcc {} not yet supported", fourcc),
        }
    }
}

impl From<EncodedFormat> for Fourcc {
    fn from(format: EncodedFormat) -> Fourcc {
        match format {
            EncodedFormat::H264 => Fourcc::from(b"H264"),
            EncodedFormat::H265 => Fourcc::from(b"H265"),
            EncodedFormat::VP8 => Fourcc::from(b"VP80"),
            EncodedFormat::VP9 => Fourcc::from(b"VP90"),
            EncodedFormat::AV1 => Fourcc::from(b"AV1F"),
        }
    }
}

/// Describes the layout of a plane within a frame.
#[derive(Debug, Default, Clone, PartialEq)]
pub struct PlaneLayout {
    /// Index of the memory buffer the plane belongs to.
    pub buffer_index: usize,
    /// Start offset of the plane within its buffer.
    pub offset: usize,
    /// Distance in bytes between two lines of data in this plane.
    pub stride: usize,
}

/// Unambiguously describes the layout of a frame.
///
/// A frame can be made of one or several memory buffers, each containing one or several planes.
/// For a given frame, this structure defines where each plane can be found.
#[derive(Debug, Default, Clone, PartialEq)]
pub struct FrameLayout {
    /// `(Fourcc, modifier)` tuple describing the arrangement of the planes.
    ///
    /// This member is enough to infer how many planes and buffers the frame has, and which
    /// buffer each plane belongs into.
    pub format: (Fourcc, u64),
    /// Size in pixels of the frame.
    pub size: Resolution,
    /// Layout of each individual plane.
    pub planes: Vec<PlaneLayout>,
}

/// Build a frame memory descriptor enum that supports multiple descriptor types.
///
/// This is useful for the case where the frames' memory backing is not decided at compile-time.
/// In this case, this macro can be used to list all the potential types supported at run-time, and
/// the selected one can be built as the program is run.
///
/// # Example
///
///
/// use cros_codecs::multiple_desc_type;
/// use cros_codecs::utils::DmabufFrame;
///
/// /// Frames' memory can be provided either by the backend, or via PRIME DMABUF handles.
/// multiple_desc_type! {
///     enum OwnedOrDmaDescriptor {
///         Owned(()),
///         Dmabuf(DmabufFrame),
///     }
/// }
///
#[macro_export]
macro_rules! multiple_desc_type {
    (enum $s:ident { $($v:ident($t:ty),)* } ) => {
        pub enum $s {
            $($v($t),)*
        }

        #[cfg(feature = "vaapi")]
        impl libva::SurfaceMemoryDescriptor for $s {
            fn add_attrs(&mut self, attrs: &mut Vec<libva::VASurfaceAttrib>) -> Option<Box<dyn std::any::Any>> {
                match self {
                    $($s::$v(desc) => desc.add_attrs(attrs),)*
                }
            }
        }
    }
}

pub fn get_format_bit_depth(format: DecodedFormat) -> usize {
    match format {
        DecodedFormat::MM21
        | DecodedFormat::AR24
        | DecodedFormat::I420
        | DecodedFormat::NV12
        | DecodedFormat::I422
        | DecodedFormat::I444
        | DecodedFormat::YV12 => 8,
        DecodedFormat::MT2T | DecodedFormat::P010 => 10,
        DecodedFormat::I010
        | DecodedFormat::I012
        | DecodedFormat::I210
        | DecodedFormat::I212
        | DecodedFormat::I410
        | DecodedFormat::I412 => 16,
    }
}

/// Returns the size required to store a frame of `format` with size `width`x`height`, without any
/// padding. This is the minimum size of the destination buffer passed to `nv12_copy` or
/// `i420_copy`.
pub fn decoded_frame_size(format: DecodedFormat, width: usize, height: usize) -> usize {
    match format {
        DecodedFormat::I420 | DecodedFormat::NV12 => {
            let u_size = width * height;
            // U and V planes need to be aligned to 2.
            let uv_size = ((width + 1) / 2) * ((height + 1) / 2) * 2;

            u_size + uv_size
        }
        DecodedFormat::I422 => {
            let u_size = width * height;
            // U and V planes need to be aligned to 2.
            let uv_size = ((width + 1) / 2) * ((height + 1) / 2) * 2 * 2;

            u_size + uv_size
        }
        DecodedFormat::I444 => (width * height) * 3,
        DecodedFormat::I010 | DecodedFormat::I012 => {
            decoded_frame_size(DecodedFormat::I420, width, height) * 2
        }
        DecodedFormat::I210 | DecodedFormat::I212 => {
            let u_size = width * height * 2;
            // U and V planes need to be aligned to 2.
            let uv_size = ((width + 1) / 2) * ((height + 1) / 2) * 2 * 2;

            u_size + uv_size
        }
        DecodedFormat::I410 | DecodedFormat::I412 => (width * height * 2) * 3,
        _ => panic!("Unable to convert to {:?}", format),
    }
}

/// Instructs on whether it should block on the operation(s).
#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub enum BlockingMode {
    Blocking,
    #[default]
    NonBlocking,
}

/// Information about the range of the stream.
///
/// This will either be full swing, or limited/"studio" swing.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum ColorRange {
    #[default]
    Unspecified = 2,
    Limited = 0,
    Full = 1,
}

impl From<bool> for ColorRange {
    fn from(value: bool) -> Self {
        if value {
            ColorRange::Full
        } else {
            ColorRange::Limited
        }
    }
}

/// Information about the color primaries of the stream.
///
/// These are encoded as an enum referring to specific standards rather than the actual XY
/// coordinates.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum ColorPrimaries {
    #[default]
    Unspecified = 2,
    BT709 = 1,
    BT470M = 4,
    BT470BG = 5,
    BT601 = 6,
    SMPTE240 = 7,
    GenericFilm = 8,
    BT2020 = 9,
    SMPTE428 = 10,
    SMPTE431 = 11,
    SMPTE432 = 12,
    EBU = 22,
}

/// These come from a union of H264 spec E-3, H265 spec H265 spec E-3, and AV1 spec 6.4.2. The
/// numerical values have been standardized across all codecs.
impl From<u32> for ColorPrimaries {
    fn from(value: u32) -> Self {
        match value {
            1 => ColorPrimaries::BT709,
            4 => ColorPrimaries::BT470M,
            5 => ColorPrimaries::BT470BG,
            6 => ColorPrimaries::BT601,
            7 => ColorPrimaries::SMPTE240,
            8 => ColorPrimaries::GenericFilm,
            9 => ColorPrimaries::BT2020,
            10 => ColorPrimaries::SMPTE428,
            11 => ColorPrimaries::SMPTE431,
            12 => ColorPrimaries::SMPTE432,
            22 => ColorPrimaries::EBU,
            _ => ColorPrimaries::Unspecified,
        }
    }
}

/// Information about the transfer function of the stream.
///
/// These are encoded as an enum for specific standards, not actual coefficients for any equations.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum TransferFunction {
    #[default]
    Unspecified = 2,
    BT709 = 1,
    BT470M = 4,
    BT470BG = 5,
    BT601 = 6,
    SMPTE240 = 7,
    Linear = 8,
    Log100 = 9,
    Log100Sqrt10 = 10,
    IEC61966 = 11,
    BT1361 = 12,
    sRGB = 13,
    BT2020TenBit = 14,
    BT2020TwelveBit = 15,
    SMPTE2084 = 16,
    SMPTE428 = 17,
    HLG = 18,
}

/// These come from a union of H264 spec E-4, H265 spec H265 spec E-4, and AV1 spec 6.4.2. The
/// numerical values have been standardized across all codecs.
impl From<u32> for TransferFunction {
    fn from(value: u32) -> Self {
        match value {
            1 => TransferFunction::BT709,
            4 => TransferFunction::BT470M,
            5 => TransferFunction::BT470BG,
            6 => TransferFunction::BT601,
            7 => TransferFunction::SMPTE240,
            8 => TransferFunction::Linear,
            9 => TransferFunction::Log100,
            10 => TransferFunction::Log100Sqrt10,
            11 => TransferFunction::IEC61966,
            12 => TransferFunction::BT1361,
            13 => TransferFunction::sRGB,
            14 => TransferFunction::BT2020TenBit,
            15 => TransferFunction::BT2020TwelveBit,
            16 => TransferFunction::SMPTE2084,
            17 => TransferFunction::SMPTE428,
            18 => TransferFunction::HLG,
            _ => TransferFunction::Unspecified,
        }
    }
}

/// Information about the YUV->RGB conversion matrix.
///
/// These are encoded as an enum for specific standards, not the actual coefficients.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum MatrixCoefficients {
    #[default]
    Unspecified = 2,
    Identity = 0,
    BT709 = 1,
    FCC = 4,
    BT470BG = 5,
    BT601 = 6,
    SMPTE240M = 7,
    YCgCo = 8,
    BT2020NonConst = 9,
    BT2020Const = 10,
    SMPTE2085 = 11,
    ChromNonConst = 12,
    ChromConst = 13,
    BT2100 = 14,
    IPTC2 = 15,
    YCgCoRe = 16,
    YCgCoRo = 17,
}

/// These come from a union of H264 spec E-5, H265 spec H265 spec E-5, and AV1 spec 6.4.2. The
/// numerical values have been standardized across all codecs.
impl From<u32> for MatrixCoefficients {
    fn from(value: u32) -> Self {
        match value {
            0 => MatrixCoefficients::Identity,
            1 => MatrixCoefficients::BT709,
            4 => MatrixCoefficients::FCC,
            5 => MatrixCoefficients::BT470BG,
            6 => MatrixCoefficients::BT601,
            7 => MatrixCoefficients::SMPTE240M,
            8 => MatrixCoefficients::YCgCo,
            9 => MatrixCoefficients::BT2020NonConst,
            10 => MatrixCoefficients::BT2020Const,
            11 => MatrixCoefficients::SMPTE2085,
            12 => MatrixCoefficients::ChromNonConst,
            13 => MatrixCoefficients::ChromConst,
            14 => MatrixCoefficients::BT2100,
            15 => MatrixCoefficients::IPTC2,
            16 => MatrixCoefficients::YCgCoRe,
            17 => MatrixCoefficients::YCgCoRo,
            _ => MatrixCoefficients::Unspecified,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Fourcc;

    const NV12_FOURCC: u32 = 0x3231564E;

    #[test]
    fn fourcc_u32() {
        let fourcc = Fourcc::from(NV12_FOURCC);
        let value: u32 = fourcc.into();
        assert_eq!(value, NV12_FOURCC);
    }

    #[test]
    fn fourcc_u8_4() {
        let fourcc = Fourcc::from(NV12_FOURCC);
        let value: [u8; 4] = fourcc.into();
        assert_eq!(value, *b"NV12");
    }

    #[test]
    fn fourcc_display() {
        let fourcc = Fourcc::from(NV12_FOURCC);
        assert_eq!(fourcc.to_string(), "NV12");
    }

    #[test]
    fn fourcc_debug() {
        let fourcc = Fourcc::from(NV12_FOURCC);
        assert_eq!(format!("{:?}", fourcc), "0x3231564e (NV12)");
    }
}
