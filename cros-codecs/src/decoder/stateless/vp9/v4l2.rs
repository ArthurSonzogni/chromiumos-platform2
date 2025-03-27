// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use v4l2r::device::Device as VideoDevice;
use v4l2r::ioctl;
use v4l2r::ioctl::CtrlWhich;
use v4l2r::ioctl::FormatIterator;
use v4l2r::QueueType;

use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderHandle;
use crate::backend::v4l2::decoder::V4l2StreamInfo;
use crate::backend::v4l2::decoder::ADDITIONAL_REFERENCE_FRAME_BUFFER;
use crate::codec::vp9::parser::Header;
use crate::codec::vp9::parser::Segmentation;
use crate::codec::vp9::parser::ALTREF_FRAME;
use crate::codec::vp9::parser::GOLDEN_FRAME;
use crate::codec::vp9::parser::LAST_FRAME;
use crate::codec::vp9::parser::MAX_SEGMENTS;
use crate::codec::vp9::parser::NUM_REF_FRAMES;
use crate::decoder::stateless::vp9::StatelessVp9DecoderBackend;
use crate::decoder::stateless::vp9::Vp9;
use crate::decoder::stateless::NewPictureError;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::Arc;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::device::v4l2::stateless::controls::vp9::V4l2CtrlVp9FrameParams;
use crate::device::v4l2::stateless::controls::vp9::Vp9V4l2Control;

use crate::video_frame::VideoFrame;
use crate::DecodedFormat;
use crate::Fourcc;
use crate::Rect;
use crate::Resolution;

impl V4l2StreamInfo for &Header {
    fn min_num_frames(&self) -> usize {
        NUM_REF_FRAMES + ADDITIONAL_REFERENCE_FRAME_BUFFER
    }

    fn coded_size(&self) -> Resolution {
        Resolution::from((self.width, self.height))
    }

    fn visible_rect(&self) -> Rect {
        Rect::from(((0, 0), (self.render_width, self.render_height)))
    }

    fn bit_depth(&self) -> usize {
        self.bit_depth as usize
    }

    fn get_decoded_format(&self, device: Arc<VideoDevice>) -> Result<DecodedFormat, String> {
        if self.bit_depth() == 10 {
            let mut vp9_frame_params = V4l2CtrlVp9FrameParams::new();
            vp9_frame_params.set_10bit_params();
            let mut ctrl = Vp9V4l2Control::from(&vp9_frame_params);

            ioctl::s_ext_ctrls(&device, CtrlWhich::Current, &mut ctrl)
                .map_err(|_| String::from("Failed to send ext ctrls to device."))?;
        }

        Ok(FormatIterator::new(&device, QueueType::VideoCaptureMplane)
            .map(|x| Fourcc(x.pixelformat.into()))
            .next()
            .unwrap()
            .into())
    }
}

impl<V: VideoFrame> StatelessDecoderBackendPicture<Vp9> for V4l2StatelessDecoderBackend<V> {
    type Picture = Rc<RefCell<V4l2Picture<V>>>;
}

impl<V: VideoFrame> StatelessVp9DecoderBackend for V4l2StatelessDecoderBackend<V> {
    fn new_sequence(&mut self, header: &Header) -> StatelessBackendResult<()> {
        self.new_sequence(header, Fourcc::from(b"VP9F"))
    }

    fn new_picture(
        &mut self,
        timestamp: u64,
        alloc_cb: &mut dyn FnMut() -> Option<
            <<Self as StatelessDecoderBackend>::Handle as DecodedHandle>::Frame,
        >,
    ) -> NewPictureResult<Self::Picture> {
        let frame_id = (timestamp << 1) | (self.frame_counter & 1);
        let frame = alloc_cb().ok_or(NewPictureError::OutOfOutputBuffers)?;
        let request_buffer = match self.device.alloc_request(frame_id, frame) {
            Ok(buffer) => buffer,
            _ => return Err(NewPictureError::OutOfOutputBuffers),
        };
        let picture = Rc::new(RefCell::new(V4l2Picture::new(request_buffer.clone())));
        request_buffer
            .as_ref()
            .borrow_mut()
            .set_picture_ref(Rc::<RefCell<V4l2Picture<V>>>::downgrade(&picture));

        self.frame_counter = self.frame_counter + 1;
        Ok(picture)
    }

    fn new_handle_from_existing_handle(
        &mut self,
        existing_handle: &Self::Handle,
        timestamp: u64,
    ) -> NewPictureResult<Self::Handle> {
        Ok(existing_handle.new_handle_from_same_buffer(timestamp))
    }

    fn submit_picture(
        &mut self,
        picture: Self::Picture,
        hdr: &Header,
        reference_frames: &[Option<Self::Handle>; NUM_REF_FRAMES],
        bitstream: &[u8],
        _segmentation: &[Segmentation; MAX_SEGMENTS],
    ) -> StatelessBackendResult<Self::Handle> {
        let mut vp9_frame_params = V4l2CtrlVp9FrameParams::new();

        let last_frame_idx = hdr.ref_frame_idx[LAST_FRAME - 1];
        let golden_frame_idx = hdr.ref_frame_idx[GOLDEN_FRAME - 1];
        let alt_frame_idx = hdr.ref_frame_idx[ALTREF_FRAME - 1];

        let last_frame_ts = match &reference_frames[last_frame_idx as usize] {
            Some(handle) => handle.timestamp(),
            None => 0,
        };

        let golden_frame_ts = match &reference_frames[golden_frame_idx as usize] {
            Some(handle) => handle.timestamp(),
            None => 0,
        };

        let alt_frame_ts = match &reference_frames[alt_frame_idx as usize] {
            Some(handle) => handle.timestamp(),
            None => 0,
        };

        vp9_frame_params
            .set_loop_filter_params(hdr)
            .set_quantization_params(hdr)
            .set_segmentation_params(hdr)
            .set_frame_params(hdr, last_frame_ts, golden_frame_ts, alt_frame_ts);

        let mut ctrl = Vp9V4l2Control::from(&vp9_frame_params);

        let request = picture.borrow_mut().request();
        let mut request = request.as_ref().borrow_mut();

        // We have to do this manually since v4l2r does not directly support VP9
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        let mut reference_pictures = Vec::<Rc<RefCell<V4l2Picture<V>>>>::new();
        for frame in reference_frames {
            if frame.is_some() {
                reference_pictures.push(frame.as_ref().unwrap().picture.clone());
            }
        }
        picture.borrow_mut().set_ref_pictures(reference_pictures);

        request.write(bitstream);
        request.submit()?;

        Ok(V4l2StatelessDecoderHandle {
            picture: picture.clone(),
            stream_info: self.stream_info.clone(),
            override_timestamp: None,
        })
    }
}

impl<V: VideoFrame> StatelessDecoder<Vp9, V4l2StatelessDecoderBackend<V>> {
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Result<Self, NewStatelessDecoderError> {
        Self::new(V4l2StatelessDecoderBackend::new()?, blocking_mode)
    }
}
