// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::backend::v4l2::decoder::V4l2StreamInfo;
use crate::backend::v4l2::decoder::ADDITIONAL_REFERENCE_FRAME_BUFFER;
use crate::codec::h265::dpb::Dpb;
use crate::codec::h265::parser::Pps;
use crate::codec::h265::parser::Slice;
use crate::codec::h265::parser::Sps;
use crate::codec::h265::picture::PictureData;
use crate::decoder::stateless::h265::RefPicListEntry;
use crate::decoder::stateless::h265::RefPicSet;
use crate::decoder::stateless::h265::StatelessH265DecoderBackend;
use crate::decoder::stateless::h265::H265;
use crate::decoder::stateless::NewPictureError;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::video_frame::VideoFrame;
use crate::Fourcc;
use crate::Rect;
use crate::Resolution;

impl V4l2StreamInfo for &Sps {
    fn min_num_frames(&self) -> usize {
        self.max_dpb_size() + ADDITIONAL_REFERENCE_FRAME_BUFFER
    }

    fn coded_size(&self) -> Resolution {
        Resolution::from((self.width() as u32, self.height() as u32))
    }

    fn visible_rect(&self) -> Rect {
        let rect = self.visible_rectangle();

        Rect { x: rect.min.x, y: rect.min.y, width: rect.max.x, height: rect.max.y }
    }

    fn bit_depth(&self) -> usize {
        (self.bit_depth_chroma_minus8 + 8) as usize
    }
}

impl<V: VideoFrame> StatelessDecoderBackendPicture<H265> for V4l2StatelessDecoderBackend<V> {
    type Picture = Rc<RefCell<V4l2Picture<V>>>;
}

impl<V: VideoFrame> StatelessH265DecoderBackend for V4l2StatelessDecoderBackend<V> {
    fn new_sequence(&mut self, sps: &Sps) -> StatelessBackendResult<()> {
        self.new_sequence(sps, Fourcc::from(b"S265"))
    }

    fn new_picture(
        &mut self,
        timestamp: u64,
        alloc_cb: &mut dyn FnMut() -> Option<
            <<Self as StatelessDecoderBackend>::Handle as DecodedHandle>::Frame,
        >,
    ) -> NewPictureResult<Self::Picture> {
        let frame = alloc_cb().ok_or(NewPictureError::OutOfOutputBuffers)?;
        let request_buffer = match self.device.alloc_request(timestamp, frame) {
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

    fn begin_picture(
        &mut self,
        _picture: &mut Self::Picture,
        _picture_data: &PictureData,
        _sps: &Sps,
        _pps: &Pps,
        _dpb: &Dpb<Self::Handle>,
        _rps: &RefPicSet<Self::Handle>,
        _slice: &Slice,
    ) -> crate::decoder::stateless::StatelessBackendResult<()> {
        todo!()
    }

    fn decode_slice(
        &mut self,
        _picture: &mut Self::Picture,
        _slice: &Slice,
        _sps: &Sps,
        _: &Pps,
        _ref_pic_list0: &[Option<RefPicListEntry<Self::Handle>>; 16],
        _ref_pic_list1: &[Option<RefPicListEntry<Self::Handle>>; 16],
    ) -> crate::decoder::stateless::StatelessBackendResult<()> {
        todo!()
    }

    fn submit_picture(
        &mut self,
        mut _picture: Self::Picture,
    ) -> StatelessBackendResult<Self::Handle> {
        todo!()
    }
}

impl<V: VideoFrame> StatelessDecoder<H265, V4l2StatelessDecoderBackend<V>> {
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Result<Self, NewStatelessDecoderError> {
        Self::new(V4l2StatelessDecoderBackend::new()?, blocking_mode)
    }
}
