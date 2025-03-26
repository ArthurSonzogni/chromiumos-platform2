// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;

use v4l2r::bindings;
use v4l2r::ioctl;
use v4l2r::ioctl::CtrlWhich;

use crate::backend::v4l2::decoder::V4l2StreamInfo;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::DecodedHandle;
use crate::decoder::StreamInfo;
use crate::device::v4l2::stateless::controls::av1::Av1V4l2SequenceCtrl;
use crate::device::v4l2::stateless::controls::av1::V4l2CtrlAv1SequenceParams;
use crate::device::v4l2::stateless::controls::vp9::V4l2CtrlVp9FrameParams;
use crate::device::v4l2::stateless::controls::vp9::Vp9V4l2Control;
use crate::device::v4l2::stateless::device::V4l2Device;
use crate::device::v4l2::stateless::request::V4l2Request;
use crate::device::v4l2::utils::get_decoded_format;

use crate::video_frame::VideoFrame;
use crate::DecodedFormat;
use crate::Fourcc;
use crate::Resolution;

pub struct V4l2Picture<V: VideoFrame> {
    request: Rc<RefCell<V4l2Request<V>>>,
    // To properly decode stream while output and capture queues
    // are processed independently it's required for v4l2 backend
    // to maintain DPB buffer recycling. The following vector
    // is used to prevent reference pictures to be reused while
    // current picture is still being decoded.
    ref_pictures: Option<Vec<Rc<RefCell<V4l2Picture<V>>>>>,
}

impl<V: VideoFrame> V4l2Picture<V> {
    pub fn new(request: Rc<RefCell<V4l2Request<V>>>) -> Self {
        Self { request, ref_pictures: None }
    }
    pub fn video_frame(&self) -> Arc<V> {
        self.request.as_ref().borrow().result().capture_buffer.borrow().frame.clone()
    }
    pub fn timestamp(&self) -> u64 {
        self.request.as_ref().borrow().timestamp()
    }
    pub fn set_ref_pictures(
        &mut self,
        ref_pictures: Vec<Rc<RefCell<V4l2Picture<V>>>>,
    ) -> &mut Self {
        self.ref_pictures = Some(ref_pictures);
        self
    }
    pub fn sync(&mut self) -> &mut Self {
        self.request.as_ref().borrow_mut().sync();
        self.ref_pictures = None;
        self
    }
    pub fn request(&mut self) -> Rc<RefCell<V4l2Request<V>>> {
        self.request.clone()
    }
    pub fn drop_references(&mut self) {
        self.ref_pictures = None;
    }
}

pub struct V4l2StatelessDecoderHandle<V: VideoFrame> {
    pub picture: Rc<RefCell<V4l2Picture<V>>>,
    pub stream_info: StreamInfo,
}

impl<V: VideoFrame> Clone for V4l2StatelessDecoderHandle<V> {
    fn clone(&self) -> Self {
        Self { picture: Rc::clone(&self.picture), stream_info: self.stream_info.clone() }
    }
}

impl<V: VideoFrame> DecodedHandle for V4l2StatelessDecoderHandle<V> {
    type Frame = V;

    fn video_frame(&self) -> Arc<Self::Frame> {
        self.picture.borrow().video_frame()
    }

    fn coded_resolution(&self) -> Resolution {
        self.stream_info.coded_resolution.clone()
    }

    fn display_resolution(&self) -> Resolution {
        self.stream_info.display_resolution.clone()
    }

    fn timestamp(&self) -> u64 {
        self.picture.borrow().timestamp()
    }

    fn sync(&self) -> anyhow::Result<()> {
        self.picture.borrow_mut().sync();
        Ok(())
    }

    fn is_ready(&self) -> bool {
        todo!();
    }
}

pub struct V4l2StatelessDecoderBackend<V: VideoFrame> {
    pub device: V4l2Device<V>,
    pub stream_info: StreamInfo,
    pub frame_counter: u64,
}

impl<V: VideoFrame> V4l2StatelessDecoderBackend<V> {
    pub fn new() -> Result<Self, NewStatelessDecoderError> {
        Ok(Self {
            device: V4l2Device::new()?,
            stream_info: StreamInfo {
                format: DecodedFormat::I420,
                min_num_frames: 0,
                coded_resolution: Resolution::from((0, 0)),
                display_resolution: Resolution::from((0, 0)),
            },
            frame_counter: 0,
        })
    }

    // Sends a 10 bit frame header to the OUTPUT queue to signal to the device that the
    // new stream has a bit depth of 10 bits.
    pub fn set_ext_ctrl_10bit(&mut self, fourcc: Fourcc) -> StatelessBackendResult<()> {
        match fourcc.to_string().as_str() {
            "VP9F" => {
                let mut vp9_frame_params = V4l2CtrlVp9FrameParams::new();
                vp9_frame_params.set_10bit_params();
                let mut ctrl = Vp9V4l2Control::from(&vp9_frame_params);

                ioctl::s_ext_ctrls(&self.device, CtrlWhich::Current, &mut ctrl)
                    .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;
            }
            "AV1F" => {
                let mut av1_seq_params = V4l2CtrlAv1SequenceParams::new();
                av1_seq_params.set_10bit_params();
                let mut ctrl = Av1V4l2SequenceCtrl::from(&av1_seq_params);

                ioctl::s_ext_ctrls(&self.device, CtrlWhich::Current, &mut ctrl)
                    .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;
            }
            // TODO(bchoobineh): Support 10 bit H.264/H.265.
            _ => {
                log::info!("Currently do not support this fourcc for 10 bit bitstreams");
                return Err(StatelessBackendError::UnsupportedProfile);
            }
        }
        Ok(())
    }

    pub(crate) fn new_sequence<StreamData>(
        &mut self,
        stream_params: &StreamData,
        fourcc: Fourcc,
    ) -> StatelessBackendResult<()>
    where
        for<'a> &'a StreamData: V4l2StreamInfo,
    {
        let coded_resolution = stream_params.coded_size().clone();
        let min_num_frames = stream_params.min_num_frames();

        self.device.initialize_output_queue(fourcc, coded_resolution, min_num_frames as u32)?;

        // Send a fake header to handle 10 bit streams
        if stream_params.bit_depth() == 10 {
            self.set_ext_ctrl_10bit(fourcc);
        }

        self.stream_info.format = get_decoded_format(self.device.get_video_device());
        self.stream_info.display_resolution = Resolution::from(stream_params.visible_rect());
        self.stream_info.coded_resolution = coded_resolution;
        self.stream_info.min_num_frames = min_num_frames;

        Ok(self.device.initialize_capture_queue(min_num_frames as u32)?)
    }
}

impl<V: VideoFrame> StatelessDecoderBackend for V4l2StatelessDecoderBackend<V> {
    type Handle = V4l2StatelessDecoderHandle<V>;

    fn stream_info(&self) -> Option<&StreamInfo> {
        // TODO
        Some(&self.stream_info)
    }

    fn reset_backend(&mut self) -> anyhow::Result<()> {
        Ok(self.device.reset_queues()?)
    }
}
