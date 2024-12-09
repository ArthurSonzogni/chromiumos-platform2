// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use v4l2r::controls::SafeExtControl;

use crate::backend::v4l2::decoder::stateless::BackendHandle;
use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderHandle;

use crate::codec::vp8::parser::Header;
use crate::codec::vp8::parser::MbLfAdjustments;
use crate::codec::vp8::parser::Segmentation;

use crate::decoder::stateless::vp8::StatelessVp8DecoderBackend;
use crate::decoder::stateless::vp8::Vp8;

use crate::decoder::stateless::NewPictureError;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::BlockingMode;

use crate::Resolution;

impl StatelessDecoderBackendPicture<Vp8> for V4l2StatelessDecoderBackend {
    type Picture = Rc<RefCell<V4l2Picture>>;
}

impl StatelessVp8DecoderBackend for V4l2StatelessDecoderBackend {
    fn new_sequence(&mut self, _: &Header) -> StatelessBackendResult<()> {
        Ok(())
    }

    fn new_picture(&mut self, timestamp: u64) -> NewPictureResult<Self::Picture> {
        let request_buffer = match self.device.alloc_request(timestamp) {
            Ok(buffer) => buffer,
            _ => return Err(NewPictureError::OutOfOutputBuffers),
        };
        Ok(Rc::new(RefCell::new(V4l2Picture::new(request_buffer))))
    }

    fn submit_picture(
        &mut self,
        picture: Self::Picture,
        _: &Header,
        _: &Option<Self::Handle>,
        _: &Option<Self::Handle>,
        _: &Option<Self::Handle>,
        _: &[u8],
        _: &Segmentation,
        _: &MbLfAdjustments,
    ) -> StatelessBackendResult<Self::Handle> {
        let handle = Rc::new(RefCell::new(BackendHandle {
            picture: picture.clone(),
        }));
        println!(
            "{:<20} {:?}\n",
            "submit_picture",
            picture.borrow().timestamp()
        );
        picture.borrow_mut().request().submit();
        Ok(V4l2StatelessDecoderHandle { handle })
    }
}

impl StatelessDecoder<Vp8, V4l2StatelessDecoderBackend> {
    // Creates a new instance of the decoder using the v4l2 backend.
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Self {
        Self::new(V4l2StatelessDecoderBackend::new(), blocking_mode)
            .expect("Failed to create v4l2 stateless decoder backend")
    }
}
