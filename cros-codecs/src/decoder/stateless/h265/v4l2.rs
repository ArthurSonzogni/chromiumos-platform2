// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::rc::Rc;

use v4l2r::bindings::v4l2_ctrl_hevc_pps;
use v4l2r::bindings::v4l2_ctrl_hevc_scaling_matrix;
use v4l2r::bindings::v4l2_ctrl_hevc_sps;
use v4l2r::device::Device as VideoDevice;
use v4l2r::ioctl;
use v4l2r::ioctl::FormatIterator;
use v4l2r::QueueType;

use crate::backend::v4l2::decoder::stateless::V4l2Picture;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderBackend;
use crate::backend::v4l2::decoder::stateless::V4l2StatelessDecoderHandle;
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
use crate::decoder::stateless::StatelessBackendError;
use crate::decoder::stateless::StatelessBackendResult;
use crate::decoder::stateless::StatelessDecoder;
use crate::decoder::stateless::StatelessDecoderBackend;
use crate::decoder::stateless::StatelessDecoderBackendPicture;
use crate::decoder::Arc;
use crate::decoder::BlockingMode;
use crate::decoder::DecodedHandle;
use crate::device::v4l2::stateless::controls::h265::HevcV4l2DecodeParams;
use crate::device::v4l2::stateless::controls::h265::HevcV4l2Pps;
use crate::device::v4l2::stateless::controls::h265::HevcV4l2ScalingMatrix;
use crate::device::v4l2::stateless::controls::h265::HevcV4l2Sps;
use crate::device::v4l2::stateless::controls::h265::V4l2CtrlHEVCDecodeParams;
use crate::device::v4l2::stateless::controls::h265::V4l2CtrlHEVCDpbEntry;
use crate::video_frame::VideoFrame;
use crate::DecodedFormat;
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

    fn get_decoded_format(&self, device: Arc<VideoDevice>) -> Result<DecodedFormat, String> {
        // TODO(bchoobineh): Support 10-bit HEVC
        Ok(FormatIterator::new(&device, QueueType::VideoCaptureMplane)
            .map(|x| Fourcc(x.pixelformat.into()))
            .next()
            .unwrap()
            .into())
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
        picture: &mut Self::Picture,
        picture_data: &PictureData,
        sps: &Sps,
        pps: &Pps,
        dpb: &Dpb<Self::Handle>,
        _rps: &RefPicSet<Self::Handle>,
        slice: &Slice,
    ) -> crate::decoder::stateless::StatelessBackendResult<()> {
        let mut dpb_entries = Vec::<V4l2CtrlHEVCDpbEntry>::new();
        let mut ref_pictures = Vec::<Rc<RefCell<V4l2Picture<V>>>>::new();
        for entry in dpb.entries() {
            let ref_picture = entry.1.picture.clone();
            dpb_entries.push(V4l2CtrlHEVCDpbEntry {
                timestamp: ref_picture.borrow().timestamp(),
                pic: (*entry.0).clone().into_inner(),
            });
            ref_pictures.push(ref_picture);
        }

        let mut h265_decode_params = V4l2CtrlHEVCDecodeParams::new();
        let h265_sps = v4l2_ctrl_hevc_sps::from(sps);
        let h265_pps = v4l2_ctrl_hevc_pps::from(pps);
        let h265_scaling_matrix = v4l2_ctrl_hevc_scaling_matrix::from(pps);

        h265_decode_params
            .set_picture_data(picture_data)
            .set_dpb_entries(dpb_entries)
            .set_slice_header(&slice.header);
        let mut picture = picture.borrow_mut();

        let request = picture.request();
        let request = request.as_ref().borrow_mut();

        let mut sps_ctrl = HevcV4l2Sps::from(&h265_sps);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut sps_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        let mut pps_ctrl = HevcV4l2Pps::from(&h265_pps);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut pps_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        let mut scaling_mat_ctrl = HevcV4l2ScalingMatrix::from(&h265_scaling_matrix);
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut scaling_mat_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        let mut decode_param_ctrl = HevcV4l2DecodeParams::from(&h265_decode_params.handle());
        let which = request.which();
        ioctl::s_ext_ctrls(&self.device, which, &mut decode_param_ctrl)
            .map_err(|e| StatelessBackendError::Other(anyhow::anyhow!(e)))?;

        picture.set_ref_pictures(ref_pictures);

        Ok(())
    }

    fn decode_slice(
        &mut self,
        picture: &mut Self::Picture,
        slice: &Slice,
        _sps: &Sps,
        _: &Pps,
        _ref_pic_list0: &[Option<RefPicListEntry<Self::Handle>>; 16],
        _ref_pic_list1: &[Option<RefPicListEntry<Self::Handle>>; 16],
    ) -> crate::decoder::stateless::StatelessBackendResult<()> {
        const START_CODE: [u8; 3] = [0, 0, 1];

        let request = picture.borrow_mut().request();
        let mut request = request.as_ref().borrow_mut();

        request.write(&START_CODE);
        request.write(slice.nalu.as_ref());
        Ok(())
    }

    fn submit_picture(&mut self, picture: Self::Picture) -> StatelessBackendResult<Self::Handle> {
        let request = picture.borrow_mut().request();
        let mut request = request.as_ref().borrow_mut();
        request.submit()?;
        Ok(V4l2StatelessDecoderHandle {
            picture: picture.clone(),
            stream_info: self.stream_info.clone(),
        })
    }
}

impl<V: VideoFrame> StatelessDecoder<H265, V4l2StatelessDecoderBackend<V>> {
    pub fn new_v4l2(blocking_mode: BlockingMode) -> Result<Self, NewStatelessDecoderError> {
        Self::new(V4l2StatelessDecoderBackend::new()?, blocking_mode)
    }
}
