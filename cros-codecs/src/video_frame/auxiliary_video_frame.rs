// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;
use std::sync::Arc;
use std::sync::Mutex;

use crate::decoder::StreamInfo;
use crate::video_frame::ReadMapping;
use crate::video_frame::VideoFrame;
use crate::video_frame::WriteMapping;
use crate::Fourcc;
use crate::Resolution;

#[cfg(feature = "v4l2")]
use crate::v4l2r::device::Device;
#[cfg(feature = "vaapi")]
use libva::Display;
#[cfg(feature = "v4l2")]
use v4l2r::bindings::v4l2_plane;
#[cfg(feature = "v4l2")]
use v4l2r::ioctl::V4l2Buffer;
#[cfg(feature = "v4l2")]
use v4l2r::Format;

// This datastructure is designed to tie the lifetimes of two different video frames together. This
// is useful for setting up an auxiliary frame pool where we need to make sure that each frame in
// the auxiliary pool is backed up by a frame in the output frame pool, or else image processing
// won't succeed.
#[derive(Debug)]
pub struct AuxiliaryVideoFrame<I: VideoFrame, E: VideoFrame> {
    pub internal: I,
    // We need to take ownership of the external frame to send it back to the client.
    pub external: Mutex<Option<E>>,
}

// By default the AuxiliaryVideoFrame will proxy VideoFrame methods to its "internal" VideoFrame,
// designed to represent the Cros-Codecs allocated frame.
impl<I: VideoFrame, E: VideoFrame> VideoFrame for AuxiliaryVideoFrame<I, E> {
    #[cfg(feature = "vaapi")]
    type MemDescriptor = I::MemDescriptor;

    type NativeHandle = I::NativeHandle;

    fn fourcc(&self) -> Fourcc {
        self.internal.fourcc()
    }

    fn resolution(&self) -> Resolution {
        self.internal.resolution()
    }

    fn get_plane_size(&self) -> Vec<usize> {
        self.internal.get_plane_size()
    }

    fn get_plane_pitch(&self) -> Vec<usize> {
        self.internal.get_plane_pitch()
    }

    fn map<'a>(&'a self) -> Result<Box<dyn ReadMapping<'a> + 'a>, String> {
        self.internal.map()
    }

    fn map_mut<'a>(&'a mut self) -> Result<Box<dyn WriteMapping<'a> + 'a>, String> {
        self.internal.map_mut()
    }

    #[cfg(feature = "v4l2")]
    fn fill_v4l2_plane(&self, index: usize, plane: &mut v4l2_plane) {
        self.internal.fill_v4l2_plane(index, plane)
    }

    #[cfg(feature = "v4l2")]
    fn process_dqbuf(&mut self, device: Arc<Device>, format: &Format, buf: &V4l2Buffer) {
        self.internal.process_dqbuf(device, format, buf)
    }

    #[cfg(feature = "vaapi")]
    fn to_native_handle(&self, display: &Rc<Display>) -> Result<Self::NativeHandle, String> {
        self.internal.to_native_handle(display)
    }
}
