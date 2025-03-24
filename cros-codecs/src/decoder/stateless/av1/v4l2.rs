// Copyright 2025 The ChromiumOS Authors
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
use crate::codec::av1::parser::BitDepth;
use crate::codec::av1::parser::FrameHeaderObu;
use crate::codec::av1::parser::StreamInfo;
use crate::codec::av1::parser::TileGroupObu;
use crate::codec::av1::parser::NUM_REF_FRAMES;
use crate::decoder::stateless::av1::Av1;
use crate::decoder::stateless::av1::StatelessAV1DecoderBackend;
use crate::decoder::stateless::NewPictureError;
use crate::decoder::stateless::NewPictureResult;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::decoder::stateless::StatelessBackendError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::Arc;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::device::v4l2::stateless::controls::av1::Av1V4l2FilmGrainCtrl;
use crate::device::v4l2::stateless::controls::av1::Av1V4l2FrameCtrl;
use crate::device::v4l2::stateless::controls::av1::Av1V4l2SequenceCtrl;
use crate::device::v4l2::stateless::controls::av1::Av1V4l2TileGroupEntryCtrl;
use crate::device::v4l2::stateless::controls::av1::V4l2CtrlAv1FilmGrainParams;
use crate::device::v4l2::stateless::controls::av1::V4l2CtrlAv1FrameParams;
use crate::device::v4l2::stateless::controls::av1::V4l2CtrlAv1SequenceParams;
use crate::device::v4l2::stateless::controls::av1::V4l2CtrlAv1TileGroupEntryParams;
use crate::video_frame::VideoFrame;
use crate::DecodedFormat;
use crate::Fourcc;
use crate::Rect;
use crate::Resolution;

impl V4l2StreamInfo for &StreamInfo {
    fn min_num_frames(&self) -> usize {
        NUM_REF_FRAMES + ADDITIONAL_REFERENCE_FRAME_BUFFER
    }

    fn coded_size(&self) -> Resolution {
        Resolution::from((
            self.seq_header.max_frame_width_minus_1 as u32 + 1,
            self.seq_header.max_frame_height_minus_1 as u32 + 1,
        ))
    }

    fn visible_rect(&self) -> Rect {
        Rect::from(((0, 0), (self.render_width, self.render_height)))
    }

    fn bit_depth(&self) -> usize {
        match self.seq_header.bit_depth {
            BitDepth::Depth8 => return 8,
            BitDepth::Depth10 => return 10,
            BitDepth::Depth12 => return 12,
        }
    }

    fn get_decoded_format(&self, device: Arc<VideoDevice>) -> Result<DecodedFormat, String> {
        if self.bit_depth() == 10 {
            let mut av1_seq_params = V4l2CtrlAv1SequenceParams::new();
            av1_seq_params.set_10bit_params();
            let mut ctrl = Av1V4l2SequenceCtrl::from(&av1_seq_params);

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

impl<V: VideoFrame> StatelessDecoderBackendPicture<Av1> for V4l2StatelessDecoderBackend<V> {
    type Picture = Rc<RefCell<V4l2Picture<V>>>;
}

impl<V: VideoFrame> StatelessAV1DecoderBackend for V4l2StatelessDecoderBackend<V> {
    fn change_stream_info(&mut self, stream_info: &StreamInfo) -> StatelessBackendResult<()> {
        self.new_sequence(stream_info, Fourcc::from(b"AV1F"))
    }

    fn new_picture(
        &mut self,
        _hdr: &FrameHeaderObu,
        _timestamp: u64,
        alloc_cb: &mut dyn FnMut() -> Option<V>,
    ) -> NewPictureResult<Self::Picture> {
        let timestamp = self.frame_counter;
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

    fn begin_picture(
        &mut self,
        picture: &mut Self::Picture,
        stream_info: &StreamInfo,
        hdr: &FrameHeaderObu,
        reference_frames: &[Option<Self::Handle>; NUM_REF_FRAMES],
    ) -> StatelessBackendResult<()> {
        let mut picture = picture.borrow_mut();
        let mut reference_pictures = Vec::<Rc<RefCell<V4l2Picture<V>>>>::new();
        let mut reference_ts = [0; NUM_REF_FRAMES];

        for (i, frame) in reference_frames.iter().enumerate() {
            if let Some(handle) = frame {
                reference_ts[i] = handle.timestamp();
                reference_pictures.push(frame.as_ref().unwrap().picture.clone());
            }
        }
        picture.set_ref_pictures(reference_pictures);

        let request = picture.request();
        let request = request.as_ref().borrow_mut();

        if hdr.film_grain_params.apply_grain {
            let mut film_grain_params = V4l2CtrlAv1FilmGrainParams::new();
            film_grain_params.set_film_grain_params(&hdr);

            let mut film_grain_params_ctrl = Av1V4l2FilmGrainCtrl::from(&film_grain_params);
            let which = request.which();
            ioctl::s_ext_ctrls(&self.device, which, &mut film_grain_params_ctrl)
                .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;
        }

        let mut sequence_params = V4l2CtrlAv1SequenceParams::new();
        sequence_params.set_ctrl_sequence(&stream_info.seq_header);

        let mut sequence_params_ctrl = Av1V4l2SequenceCtrl::from(&sequence_params);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut sequence_params_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        let mut frame_params = V4l2CtrlAv1FrameParams::new();
        frame_params
            .set_frame_params(&hdr)
            .set_global_motion_params(&hdr.global_motion_params)
            .set_loop_restoration_params(&hdr.loop_restoration_params)
            .set_cdef_params(&hdr.cdef_params)
            .set_loop_filter_params(&hdr.loop_filter_params)
            .set_segmentation_params(&hdr.segmentation_params)
            .set_quantization_params(&hdr.quantization_params)
            .set_tile_info_params(&hdr)
            .set_reference_frame_ts(reference_ts);

        let mut frame_params_ctrl = Av1V4l2FrameCtrl::from(&frame_params);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut frame_params_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        Ok(())
    }

    fn decode_tile_group(
        &mut self,
        picture: &mut Self::Picture,
        tile_group: TileGroupObu,
    ) -> crate::decoder::stateless::StatelessBackendResult<()> {
        let mut tile_group_params = V4l2CtrlAv1TileGroupEntryParams::new();

        for tile in &tile_group.tiles {
            tile_group_params.set_tile_group_entry(&tile);
        }

        let mut picture = picture.borrow_mut();
        let request = picture.request();
        let mut request = request.as_ref().borrow_mut();

        let mut tile_group_params_ctrl = Av1V4l2TileGroupEntryCtrl::from(&tile_group_params);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut tile_group_params_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        request.write(tile_group.obu.as_ref());

        Ok(())
    }

    fn submit_picture(&mut self, picture: Self::Picture) -> StatelessBackendResult<Self::Handle> {
        let request = picture.borrow_mut().request();
        let mut request = request.as_ref().borrow_mut();
        request.submit()?;
        Ok(V4l2StatelessDecoderHandle {
            picture: picture.clone(),
            stream_info: self.stream_info.clone(),
            override_timestamp: None,
        })
    }
}

impl<V: VideoFrame> StatelessDecoder<Av1, V4l2StatelessDecoderBackend<V>> {
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Result<Self, NewStatelessDecoderError> {
        Self::new(V4l2StatelessDecoderBackend::new()?, blocking_mode)
    }
}
