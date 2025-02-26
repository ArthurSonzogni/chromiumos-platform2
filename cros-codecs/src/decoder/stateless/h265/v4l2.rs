// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::codec::h265::dpb::Dpb;
use crate::codec::h265::parser::Pps;
use crate::codec::h265::parser::Slice;
use crate::codec::h265::parser::Sps;
use crate::codec::h265::picture::PictureData;
use crate::decoder::stateless::h265::RefPicListEntry;
use crate::decoder::stateless::h265::RefPicSet;
use crate::decoder::stateless::h265::StatelessH265DecoderBackend;
use crate::decoder::stateless::h265::H265;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::video_frame::VideoFrame;

impl<V: VideoFrame> StatelessDecoderBackendPicture<H265> for V4l2StatelessDecoderBackend<V> {
    type Picture = Rc<RefCell<V4l2Picture<V>>>;
}

impl<V: VideoFrame> StatelessH265DecoderBackend for V4l2StatelessDecoderBackend<V> {
    fn new_sequence(&mut self, _sps: &Sps) -> StatelessBackendResult<()> {
        todo!()
    }

    fn new_picture(
        &mut self,
        _timestamp: u64,
        _alloc_cb: &mut dyn FnMut() -> Option<
            <<Self as StatelessDecoderBackend>::Handle as DecodedHandle>::Frame,
        >,
    ) -> NewPictureResult<Self::Picture> {
        todo!()
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
