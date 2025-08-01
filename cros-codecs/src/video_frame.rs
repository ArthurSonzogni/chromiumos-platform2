// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::fmt::Debug;
#[cfg(feature = "vaapi")]
use std::rc::Rc;
#[cfg(feature = "v4l2")]
use std::sync::Arc;

use crate::utils::align_up;
use crate::DecodedFormat;
use crate::EncodedFormat;
use crate::Fourcc;
use crate::Resolution;

pub mod auxiliary_video_frame;
pub mod frame_pool;
#[cfg(feature = "backend")]
pub mod gbm_video_frame;
#[cfg(feature = "backend")]
pub mod generic_dma_video_frame;
#[cfg(feature = "v4l2")]
pub mod v4l2_mmap_video_frame;

#[cfg(feature = "vaapi")]
use libva::{Display, Surface, SurfaceMemoryDescriptor};
#[cfg(feature = "v4l2")]
use v4l2r::bindings::v4l2_plane;
#[cfg(feature = "v4l2")]
use v4l2r::device::Device;
#[cfg(feature = "v4l2")]
use v4l2r::ioctl::V4l2Buffer;
#[cfg(feature = "v4l2")]
use v4l2r::memory::{BufferHandles, Memory, MemoryType, PlaneHandle, PrimitiveBufferHandles};
#[cfg(feature = "v4l2")]
use v4l2r::Format;

pub const ARGB_PLANE: usize = 0;
pub const Y_PLANE: usize = 0;
pub const UV_PLANE: usize = 1;
pub const U_PLANE: usize = 1;
pub const V_PLANE: usize = 2;

// RAII wrappers for video memory mappings. The Drop method should implement any necessary
// munmap()'ing and cache flushing.
pub trait ReadMapping<'a> {
    fn get(&self) -> Vec<&[u8]>;
}

pub trait WriteMapping<'a> {
    fn get(&self) -> Vec<RefCell<&'a mut [u8]>>;
}

// Rust doesn't allow type aliases in traits, so we use this stupid hack to accomplish effectively
// the same thing.
pub trait Equivalent<A>: From<A> + Into<A> {
    fn from_ref(value: &A) -> &Self;
    fn into_ref(self: &Self) -> &A;
}

impl<A> Equivalent<A> for A {
    fn from_ref(value: &A) -> &Self {
        value
    }
    fn into_ref(self: &Self) -> &A {
        self
    }
}

// Unified abstraction for any kind of frame data that might be sent to the hardware.
pub trait VideoFrame: Send + Sync + Sized + Debug + 'static {
    #[cfg(feature = "v4l2")]
    type NativeHandle: PlaneHandle;

    #[cfg(feature = "vaapi")]
    type MemDescriptor: SurfaceMemoryDescriptor;
    #[cfg(feature = "vaapi")]
    type NativeHandle: Equivalent<Surface<Self::MemDescriptor>>;

    fn fourcc(&self) -> Fourcc;

    fn modifier(&self) -> u64;

    // Outputs visible resolution. Use pitch and plane size for coded resolution calculations.
    fn resolution(&self) -> Resolution;

    fn is_compressed(&self) -> bool {
        match self.fourcc().to_string().as_str() {
            "H264" | "HEVC" | "VP80" | "VP90" | "AV1F" => true,
            _ => false,
        }
    }

    // Whether or not all the planes are in a contiguous memory allocation. For example, returns
    // true for NV12 and false for NM12.
    fn is_contiguous(&self) -> bool {
        if self.is_compressed() {
            return false;
        }

        // TODO: Add more formats.
        match self.fourcc().to_string().as_str() {
            "MM21" | "NM12" => false,
            _ => true,
        }
    }

    fn decoded_format(&self) -> Result<DecodedFormat, String> {
        if self.is_compressed() {
            return Err("Cannot convert compressed format into decoded format".to_string());
        }

        Ok(DecodedFormat::from(self.fourcc()))
    }

    fn encoded_format(&self) -> Result<EncodedFormat, String> {
        if !self.is_compressed() {
            return Err("Cannot convert uncompressed format into encoded format".to_string());
        }

        Ok(EncodedFormat::from(self.fourcc()))
    }

    fn num_planes(&self) -> usize {
        if self.is_compressed() {
            return 1;
        }

        match self.decoded_format().unwrap() {
            DecodedFormat::AR24 => 1,
            DecodedFormat::I420
            | DecodedFormat::I422
            | DecodedFormat::I444
            | DecodedFormat::I010
            | DecodedFormat::I012
            | DecodedFormat::I210
            | DecodedFormat::I212
            | DecodedFormat::I410
            | DecodedFormat::I412
            | DecodedFormat::YV12 => 3,
            DecodedFormat::NV12
            | DecodedFormat::MM21
            | DecodedFormat::MT2T
            | DecodedFormat::P010 => 2,
        }
    }

    fn get_horizontal_subsampling(&self) -> Vec<usize> {
        let mut ret: Vec<usize> = vec![];
        for plane_idx in 0..self.num_planes() {
            if self.is_compressed() {
                ret.push(1);
            } else {
                ret.push(match self.decoded_format().unwrap() {
                    DecodedFormat::I420
                    | DecodedFormat::NV12
                    | DecodedFormat::I422
                    | DecodedFormat::I010
                    | DecodedFormat::I012
                    | DecodedFormat::I210
                    | DecodedFormat::I212
                    | DecodedFormat::MM21
                    | DecodedFormat::MT2T
                    | DecodedFormat::P010
                    | DecodedFormat::YV12 => {
                        if plane_idx == 0 {
                            1
                        } else {
                            2
                        }
                    }
                    DecodedFormat::AR24
                    | DecodedFormat::I444
                    | DecodedFormat::I410
                    | DecodedFormat::I412 => 1,
                });
            }
        }
        ret
    }

    fn get_vertical_subsampling(&self) -> Vec<usize> {
        let mut ret: Vec<usize> = vec![];
        for plane_idx in 0..self.num_planes() {
            if self.is_compressed() {
                ret.push(1);
            } else {
                ret.push(match self.decoded_format().unwrap() {
                    DecodedFormat::I420
                    | DecodedFormat::NV12
                    | DecodedFormat::I010
                    | DecodedFormat::I012
                    | DecodedFormat::MM21
                    | DecodedFormat::MT2T
                    | DecodedFormat::P010
                    | DecodedFormat::YV12 => {
                        if plane_idx == 0 {
                            1
                        } else {
                            2
                        }
                    }
                    DecodedFormat::AR24
                    | DecodedFormat::I422
                    | DecodedFormat::I444
                    | DecodedFormat::I210
                    | DecodedFormat::I212
                    | DecodedFormat::I410
                    | DecodedFormat::I412 => 1,
                })
            }
        }
        ret
    }

    fn get_bytes_per_element(&self) -> Vec<f32> {
        let mut ret: Vec<f32> = vec![];
        for plane_idx in 0..self.num_planes() {
            if self.is_compressed() {
                ret.push(1.0);
            } else {
                ret.push(match self.decoded_format().unwrap() {
                    DecodedFormat::AR24 => 4.0,
                    DecodedFormat::I420
                    | DecodedFormat::I422
                    | DecodedFormat::I444
                    | DecodedFormat::YV12 => 1.0,
                    DecodedFormat::I010
                    | DecodedFormat::I012
                    | DecodedFormat::I210
                    | DecodedFormat::I212
                    | DecodedFormat::I410
                    | DecodedFormat::I412 => 2.0,
                    DecodedFormat::P010 => {
                        if plane_idx == 0 {
                            2.0
                        } else {
                            4.0
                        }
                    }
                    DecodedFormat::MT2T => {
                        if plane_idx == 0 {
                            1.25
                        } else {
                            2.5
                        }
                    }
                    DecodedFormat::NV12 | DecodedFormat::MM21 => {
                        if plane_idx == 0 {
                            1.0
                        } else {
                            2.0
                        }
                    }
                })
            }
        }
        ret
    }

    fn get_plane_size(&self) -> Vec<usize>;

    // Pitch is measured in bytes while stride is measured in pixels.
    fn get_plane_pitch(&self) -> Vec<usize>;

    fn validate_frame(&self) -> Result<(), String> {
        if self.is_compressed() {
            return Ok(());
        }

        let horizontal_subsampling = self.get_horizontal_subsampling();
        let vertical_subsampling = self.get_vertical_subsampling();
        let bytes_per_element = self.get_bytes_per_element();
        let plane_pitch = self.get_plane_pitch();
        let plane_size = self.get_plane_size();

        for plane in 0..self.num_planes() {
            let minimum_pitch =
                ((align_up(self.resolution().width as usize, horizontal_subsampling[plane]) as f32)
                    * (bytes_per_element[plane])
                    / (horizontal_subsampling[plane] as f32)) as usize;

            if plane_pitch[plane] < minimum_pitch {
                return Err(format!(
                    "Pitch of plane {} is insufficient to accomodate format! Expected {}, got {}",
                    plane, minimum_pitch, plane_pitch[plane]
                ));
            }
            let minimum_size =
                align_up(self.resolution().height as usize, vertical_subsampling[plane])
                    / vertical_subsampling[plane]
                    * plane_pitch[plane];
            if plane_size[plane] < minimum_size {
                return Err(format!(
                    "Size of plane {} is insufficient to accomodate format! Expected {}, got {}",
                    plane, minimum_size, plane_size[plane]
                ));
            }
        }

        Ok(())
    }

    fn map<'a>(&'a self) -> Result<Box<dyn ReadMapping<'a> + 'a>, String>;

    fn map_mut<'a>(&'a mut self) -> Result<Box<dyn WriteMapping<'a> + 'a>, String>;

    #[cfg(feature = "v4l2")]
    fn fill_v4l2_plane(&self, index: usize, plane: &mut v4l2_plane);

    #[cfg(feature = "v4l2")]
    fn process_dqbuf(&mut self, device: Arc<Device>, format: &Format, buf: &V4l2Buffer);

    #[cfg(feature = "vaapi")]
    fn to_native_handle(&self, display: &Rc<Display>) -> Result<Self::NativeHandle, String>;
}

// Rust has restrictions about implementing foreign types, so this is a stupid workaround to get
// VideoFrame to implement BufferHandles.
#[cfg(feature = "v4l2")]
#[derive(Debug)]
pub struct V4l2VideoFrame<V: VideoFrame>(pub V);

#[cfg(feature = "v4l2")]
impl<V: VideoFrame> From<V> for V4l2VideoFrame<V> {
    fn from(value: V) -> Self {
        Self(value)
    }
}

#[cfg(feature = "v4l2")]
impl<V: VideoFrame> BufferHandles for V4l2VideoFrame<V> {
    type SupportedMemoryType = MemoryType;

    fn len(&self) -> usize {
        self.0.num_planes()
    }

    fn fill_v4l2_plane(&self, index: usize, plane: &mut v4l2_plane) {
        self.0.fill_v4l2_plane(index, plane)
    }
}

#[cfg(feature = "v4l2")]
impl<V: VideoFrame> PrimitiveBufferHandles for V4l2VideoFrame<V> {
    type HandleType = V::NativeHandle;
    const MEMORY_TYPE: Self::SupportedMemoryType =
        <V::NativeHandle as PlaneHandle>::Memory::MEMORY_TYPE;
}
