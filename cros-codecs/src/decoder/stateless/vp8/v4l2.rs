// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use v4l2r::device::Device as VideoDevice;
use v4l2r::ioctl::FormatIterator;
use v4l2r::QueueType;

use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderHandle;
use crate::backend::v4l2::decoder::V4l2StreamInfo;
use crate::backend::v4l2::decoder::ADDITIONAL_REFERENCE_FRAME_BUFFER;
use crate::codec::vp8::parser::Header;
use crate::codec::vp8::parser::MbLfAdjustments;
use crate::codec::vp8::parser::Segmentation;
use crate::codec::vp8::parser::NUM_REF_FRAMES;
use crate::decoder::stateless::vp8::StatelessVp8DecoderBackend;
use crate::decoder::stateless::vp8::Vp8;
use crate::decoder::stateless::NewPictureError;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::Arc;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::device::v4l2::stateless::controls::vp8::V4l2CtrlVp8FrameParams;
use crate::video_frame::VideoFrame;
use crate::ColorPrimaries;
use crate::ColorRange;
use crate::DecodedFormat;
use crate::Fourcc;
use crate::MatrixCoefficients;
use crate::Rect;
use crate::Resolution;
use crate::TransferFunction;

impl V4l2StreamInfo for &Header {
    fn min_num_frames(&self) -> usize {
        NUM_REF_FRAMES + ADDITIONAL_REFERENCE_FRAME_BUFFER
    }

    fn coded_size(&self) -> Resolution {
        Resolution::from((self.width as u32, self.height as u32))
    }

    fn visible_rect(&self) -> Rect {
        Rect::from(self.coded_size())
    }

    fn bit_depth(&self) -> usize {
        8
    }

    fn get_decoded_format(&self, device: Arc<VideoDevice>) -> Result<DecodedFormat, String> {
        Ok(FormatIterator::new(&device, QueueType::VideoCaptureMplane)
            .map(|x| Fourcc(x.pixelformat.into()))
            .next()
            .unwrap()
            .into())
    }

    fn range(&self) -> ColorRange {
        ColorRange::Unspecified
    }

    fn primaries(&self) -> ColorPrimaries {
        ColorPrimaries::Unspecified
    }

    fn transfer(&self) -> TransferFunction {
        TransferFunction::Unspecified
    }

    fn matrix(&self) -> MatrixCoefficients {
        MatrixCoefficients::Unspecified
    }
}

impl<V: VideoFrame> StatelessDecoderBackendPicture<Vp8> for V4l2StatelessDecoderBackend<V> {
    type Picture = Rc<RefCell<V4l2Picture<V>>>;
}

impl<V: VideoFrame> StatelessVp8DecoderBackend for V4l2StatelessDecoderBackend<V> {
    fn new_sequence(&mut self, header: &Header) -> StatelessBackendResult<()> {
        self.new_sequence(header, Fourcc::from(b"VP8F"))
    }

    fn new_picture(
        &mut self,
        timestamp: u64,
        alloc_cb: &mut dyn FnMut() -> Option<
            <<Self as StatelessDecoderBackend>::Handle as DecodedHandle>::Frame,
        >,
    ) -> NewPictureResult<Self::Picture> {
        let frame_id = timestamp << 1;
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
        Ok(picture)
    }

    fn submit_picture(
        &mut self,
        picture: Self::Picture,
        hdr: &Header,
        last_ref: &Option<Self::Handle>,
        golden_ref: &Option<Self::Handle>,
        alt_ref: &Option<Self::Handle>,
        bitstream: &[u8],
        segmentation: &Segmentation,
        mb_lf_adjust: &MbLfAdjustments,
    ) -> StatelessBackendResult<Self::Handle> {
        let mut ref_pictures = Vec::<Rc<RefCell<V4l2Picture<V>>>>::new();

        let last_frame_ts = match &last_ref {
            Some(handle) => {
                ref_pictures.push(handle.picture.clone());
                handle.timestamp()
            }
            None => 0,
        };

        let golden_frame_ts = match &golden_ref {
            Some(handle) => {
                ref_pictures.push(handle.picture.clone());
                handle.timestamp()
            }
            None => 0,
        };

        let alt_frame_ts = match &alt_ref {
            Some(handle) => {
                ref_pictures.push(handle.picture.clone());
                handle.timestamp()
            }
            None => 0,
        };

        picture.borrow_mut().set_ref_pictures(ref_pictures);

        let mut vp8_frame_params = V4l2CtrlVp8FrameParams::new();
        vp8_frame_params
            .set_loop_filter_params(hdr, mb_lf_adjust)
            .set_quantization_params(hdr)
            .set_segmentation_params(segmentation)
            .set_entropy_params(hdr)
            .set_bool_ctx(hdr)
            .set_frame_params(hdr, last_frame_ts, golden_frame_ts, alt_frame_ts);

        let request = picture.borrow_mut().request();
        let mut request = request.as_ref().borrow_mut();
        request.write(bitstream);
        request.ioctl(&vp8_frame_params)?;
        request.submit()?;

        Ok(V4l2StatelessDecoderHandle {
            picture: picture.clone(),
            stream_info: self.stream_info.clone(),
            override_timestamp: None,
        })
    }
}

impl<V: VideoFrame> StatelessDecoder<Vp8, V4l2StatelessDecoderBackend<V>> {
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Result<Self, NewStatelessDecoderError> {
        Self::new(V4l2StatelessDecoderBackend::new()?, blocking_mode)
    }
}
