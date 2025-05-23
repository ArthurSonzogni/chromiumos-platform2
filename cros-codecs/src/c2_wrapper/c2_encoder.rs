// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use nix::sys::epoll::Epoll;
use nix::sys::epoll::EpollCreateFlags;
use nix::sys::epoll::EpollEvent;
use nix::sys::epoll::EpollFlags;
use nix::sys::epoll::EpollTimeout;
use nix::sys::eventfd::EventFd;

use std::collections::VecDeque;
use std::marker::PhantomData;
use std::os::fd::AsFd;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::sync::Condvar;
use std::sync::Mutex;
use std::time::Duration;

#[cfg(feature = "ubc")]
use crate::bitrate_ctrl::BitrateController;
use crate::c2_wrapper::C2EncodeJob;
use crate::c2_wrapper::C2State;
use crate::c2_wrapper::C2Status;
use crate::c2_wrapper::C2Worker;
use crate::c2_wrapper::DrainMode;
use crate::c2_wrapper::Job;
use crate::codec::av1::parser::NUM_REF_FRAMES as AV1_NUM_REF_FRAMES;
use crate::codec::vp8::parser::NUM_REF_FRAMES as VP8_NUM_REF_FRAMES;
use crate::codec::vp9::parser::NUM_REF_FRAMES as VP9_NUM_REF_FRAMES;
use crate::decoder::StreamInfo;
use crate::encoder::FrameMetadata;
use crate::encoder::RateControl;
use crate::encoder::RateControl::ConstantBitrate;
use crate::encoder::RateControl::ConstantQuality;
use crate::encoder::Tunings;
use crate::encoder::VideoEncoder;
use crate::image_processing::convert_video_frame;
use crate::image_processing::extend_border_nv12;
use crate::image_processing::SUPPORTED_CONVERSION;
#[cfg(feature = "v4l2")]
use crate::video_frame::V4l2VideoFrame;
use crate::video_frame::VideoFrame;
use crate::video_frame::UV_PLANE;
use crate::video_frame::Y_PLANE;
use crate::ColorPrimaries;
use crate::ColorRange;
use crate::DecodedFormat;
use crate::EncodedFormat;
use crate::Fourcc;
use crate::MatrixCoefficients;
use crate::Resolution;
use crate::TransferFunction;

// TODO: We should query this dynamically to avoid unnecessary conversions. In practice everything
// at least supports NV12 though.
const REAL_INPUT_FORMAT: DecodedFormat = DecodedFormat::NV12;
// TODO: Chosen arbitrarily.
const IN_FLIGHT_FRAMES: usize = 4;

pub trait C2EncoderBackend {
    type EncoderOptions: Clone + Send + 'static;

    fn new(options: Self::EncoderOptions) -> Result<Self, String>
    where
        Self: Sized;

    // Returns a new encoder, the visible resolution, and the coded resolution.
    #[cfg(feature = "vaapi")]
    fn get_encoder<V: VideoFrame>(
        &mut self,
        input_format: DecodedFormat,
        output_format: EncodedFormat,
    ) -> Result<(Box<dyn VideoEncoder<V>>, Resolution, Resolution), String>;
    #[cfg(feature = "v4l2")]
    fn get_encoder<V: VideoFrame>(
        &mut self,
        input_format: DecodedFormat,
        output_format: EncodedFormat,
    ) -> Result<(Box<dyn VideoEncoder<V4l2VideoFrame<V>>>, Resolution, Resolution), String>;
}

// If `bitstream` starts with a start code (either [0x0, 0x0, 0x1] or [0x0, 0x0, 0x0, 0x1]), this
// function returns the length of that start code (either 3 or 4). Otherwise, it returns None.
fn maybe_get_nalu_start_code_prefix(bitstream: &[u8]) -> Option<usize> {
    if bitstream.len() >= 3 && bitstream[0] == 0x0 && bitstream[1] == 0x0 && bitstream[2] == 0x1 {
        return Some(3usize);
    }
    if bitstream.len() >= 4
        && bitstream[0] == 0x0
        && bitstream[1] == 0x0
        && bitstream[2] == 0x0
        && bitstream[3] == 0x1
    {
        return Some(4usize);
    }
    None
}

// Scans `bitstream` to find the first NALU of type `nalu_type` (see the H.264 specification, Table
// 7-1 for the allowable types). If found, returns the start and end positions in `bitstream`
// corresponding to that NALU, not including the start code (such that
// `bitstream[start_pos..end_pos]` is the NALU). Otherwise, returns None.
fn find_nalu(
    bitstream: &[u8],
    nalu_type: u8,
) -> Option<(/*start_pos:*/ usize, /*end_pos:*/ usize)> {
    enum State {
        SearchingForStartCode,
        FoundStartCode,
        FoundStartOfDesiredNalu(/*start_pos: */ usize),
    }
    let mut pos = 0usize;
    let mut state = State::SearchingForStartCode;
    while pos < bitstream.len() {
        (pos, state) = match state {
            State::SearchingForStartCode => {
                match maybe_get_nalu_start_code_prefix(&bitstream[pos..]) {
                    Some(start_code_length) => (pos + start_code_length, State::FoundStartCode),
                    None => (pos + 1, State::SearchingForStartCode),
                }
            }
            State::FoundStartCode => {
                if bitstream[pos] & 0x1F == nalu_type {
                    let new_pos = pos + 1;
                    (new_pos, State::FoundStartOfDesiredNalu(/*start_pos=*/ pos))
                } else {
                    (pos + 1, State::SearchingForStartCode)
                }
            }
            State::FoundStartOfDesiredNalu(start_pos) => {
                match maybe_get_nalu_start_code_prefix(&bitstream[pos..]) {
                    Some(start_code_length) => {
                        break;
                    }
                    None => (pos + 1, State::FoundStartOfDesiredNalu(start_pos)),
                }
            }
        }
    }
    if let State::FoundStartOfDesiredNalu(start_pos) = state {
        if pos > start_pos {
            Some((start_pos, /*end_pos=*/ pos))
        } else {
            None
        }
    } else {
        None
    }
}

// If `encoded_format` is EncodedFormat::H264, this function scans `bitstream` to find the first SPS
// NALU and the first PPS NALU after that SPS. If found, returns the concatenation of them as
// follows:
//
//     [0x0, 0x0, 0x0, 0x1, ... SPS ..., 0x0, 0x0, 0x0, 0x1, ... PPS ...]
//
// If not found, returns an error.
//
// If `encoded_format` is not EncodedFormat::H264, this function returns an empty vector.
fn get_csd(encoded_format: EncodedFormat, bitstream: &[u8]) -> Result<Vec<u8>, String> {
    if encoded_format != EncodedFormat::H264 {
        // TODO(b/389993558): Also get the codec-specific data for H.265.
        return Ok(vec![]);
    }

    // First let's search for the SPS.
    let sps = find_nalu(&bitstream, 7).ok_or("Could not find the SPS")?;

    // Now let's search for the PPS but starting after the SPS.
    let bitstream_minus_first_sps = &bitstream[sps.1..];
    let pps = find_nalu(bitstream_minus_first_sps, 8).ok_or("Could not find the PPS")?;

    // Now concatenate everything together.
    let mut csd = vec![0x0, 0x0, 0x0, 0x1];
    csd.extend_from_slice(&bitstream[sps.0..sps.1]);
    csd.extend_from_slice(&[0x0, 0x0, 0x0, 0x1]);
    csd.extend_from_slice(&bitstream_minus_first_sps[pps.0..pps.1]);
    Ok(csd)
}

pub struct C2EncoderWorker<V, B>
where
    V: VideoFrame,
    B: C2EncoderBackend,
{
    #[cfg(feature = "vaapi")]
    encoder: Box<dyn VideoEncoder<V>>,
    #[cfg(feature = "v4l2")]
    encoder: Box<dyn VideoEncoder<V4l2VideoFrame<V>>>,
    awaiting_job_event: Arc<EventFd>,
    error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
    work_done_cb: Arc<Mutex<dyn FnMut(C2EncodeJob<V>) + Send + 'static>>,
    alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
    work_queue: Arc<Mutex<VecDeque<C2EncodeJob<V>>>>,
    in_flight_queue: VecDeque<C2EncodeJob<V>>,
    state: Arc<(Mutex<C2State>, Condvar)>,
    current_tunings: Tunings,
    visible_resolution: Resolution,
    coded_resolution: Resolution,
    encoded_format: EncodedFormat,
    csd_has_been_output: bool,
    #[cfg(feature = "ubc")]
    bitrate_controller: BitrateController,
    _phantom: PhantomData<B>,
}

impl<V, B> C2EncoderWorker<V, B>
where
    V: VideoFrame,
    B: C2EncoderBackend,
{
    fn poll_complete_frames(&mut self) {
        loop {
            match self.encoder.poll() {
                Ok(Some(coded)) => {
                    let mut job = self.in_flight_queue.pop_front().unwrap();
                    job.output = coded.bitstream;
                    // Only output the CSD if it hasn't been output before.
                    if !self.csd_has_been_output {
                        job.csd = match get_csd(self.encoded_format, &job.output) {
                            Ok(csd) => {
                                self.csd_has_been_output = true;
                                csd
                            }
                            Err(err) => {
                                log::debug!("Could not get the CSD! {err}");
                                *self.state.0.lock().unwrap() = C2State::C2Error;
                                (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                                break;
                            }
                        }
                    }

                    #[cfg(feature = "ubc")]
                    self.bitrate_controller.process_frame(job.output.len() as u64);

                    (*self.work_done_cb.lock().unwrap())(job);
                }
                Err(err) => {
                    log::debug!("Error during encode! {:?}", err);
                    *self.state.0.lock().unwrap() = C2State::C2Error;
                    (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                    break;
                }
                _ => break,
            }
        }
    }
}

impl<V, B> C2Worker<C2EncodeJob<V>> for C2EncoderWorker<V, B>
where
    V: VideoFrame,
    B: C2EncoderBackend,
{
    type Options = B::EncoderOptions;

    fn new(
        input_fourcc: Fourcc,
        // Note that the size of output_fourccs should only ever be 1 for encoders.
        output_fourccs: Vec<Fourcc>,
        awaiting_job_event: Arc<EventFd>,
        error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
        work_done_cb: Arc<Mutex<dyn FnMut(C2EncodeJob<V>) + Send + 'static>>,
        work_queue: Arc<Mutex<VecDeque<C2EncodeJob<V>>>>,
        state: Arc<(Mutex<C2State>, Condvar)>,
        framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
        alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
        options: Self::Options,
    ) -> Result<Self, String> {
        if output_fourccs.len() != 1 {
            return Err("Expected exactly one output fourcc!".into());
        }

        if DecodedFormat::from(input_fourcc) != REAL_INPUT_FORMAT {
            let mut conversion_support = false;
            for conversion in SUPPORTED_CONVERSION {
                if conversion.0 == DecodedFormat::from(input_fourcc)
                    && conversion.1 == REAL_INPUT_FORMAT
                {
                    conversion_support = true;
                    break;
                }
            }
            if !conversion_support {
                return Err(format!("Unsupported input format {input_fourcc}"));
            }
        }

        let encoded_format = EncodedFormat::from(output_fourccs[0]);
        let mut backend = B::new(options)?;
        let (encoder, visible_resolution, coded_resolution) =
            backend.get_encoder(REAL_INPUT_FORMAT, encoded_format)?;
        (*framepool_hint_cb.lock().unwrap())(StreamInfo {
            format: REAL_INPUT_FORMAT,
            coded_resolution: coded_resolution.clone(),
            display_resolution: visible_resolution.clone(),
            min_num_frames: match encoded_format {
                // TODO: Chosen arbitrarily. Do we ever use more than one reference frame for H264?
                EncodedFormat::H264 | EncodedFormat::H265 => 4,
                EncodedFormat::VP8 => VP8_NUM_REF_FRAMES,
                EncodedFormat::VP9 => VP9_NUM_REF_FRAMES,
                EncodedFormat::AV1 => AV1_NUM_REF_FRAMES,
            } + IN_FLIGHT_FRAMES,
            range: Default::default(),
            primaries: Default::default(),
            transfer: Default::default(),
            matrix: Default::default(),
        });

        #[cfg(feature = "ubc")]
        {
            let (min_qp, max_qp) = match encoded_format {
                EncodedFormat::AV1 | EncodedFormat::VP9 => (0, 255),
                EncodedFormat::VP8 => (0, 127),
                EncodedFormat::H264 | EncodedFormat::H265 => (1, 51),
            };
            let init_bitrate: u64 = 200000;
            let init_framerate: u32 = 30;
            let init_tunings = Tunings {
                rate_control: RateControl::ConstantBitrate(init_bitrate),
                framerate: init_framerate,
                min_quality: min_qp,
                max_quality: max_qp,
            };
            let bitrate_controller =
                BitrateController::new(visible_resolution, init_bitrate, init_framerate);

            Ok(Self {
                encoder: encoder,
                awaiting_job_event: awaiting_job_event,
                error_cb: error_cb,
                work_done_cb: work_done_cb,
                work_queue: work_queue,
                in_flight_queue: VecDeque::new(),
                state: state,
                alloc_cb: alloc_cb,
                current_tunings: init_tunings,
                visible_resolution: visible_resolution,
                coded_resolution: coded_resolution,
                encoded_format: encoded_format,
                csd_has_been_output: false,
                bitrate_controller: bitrate_controller,
                _phantom: Default::default(),
            })
        }
        #[cfg(not(feature = "ubc"))]
        {
            Ok(Self {
                encoder: encoder,
                awaiting_job_event: awaiting_job_event,
                error_cb: error_cb,
                work_done_cb: work_done_cb,
                work_queue: work_queue,
                in_flight_queue: VecDeque::new(),
                state: state,
                alloc_cb: alloc_cb,
                current_tunings: Default::default(),
                visible_resolution: visible_resolution,
                coded_resolution: coded_resolution,
                encoded_format: encoded_format,
                csd_has_been_output: false,
                _phantom: Default::default(),
            })
        }
    }

    fn process_loop(&mut self) {
        let epoll_fd = Epoll::new(EpollCreateFlags::empty()).expect("Failed to create Epoll");
        let _ = epoll_fd
            .add(self.awaiting_job_event.as_fd(), EpollEvent::new(EpollFlags::EPOLLIN, 1))
            .expect("Failed to add job event to Epoll");

        while *self.state.0.lock().unwrap() == C2State::C2Running {
            let mut events = [EpollEvent::empty()];
            // We need an actual timeout because the encoder is poll based rather than async.
            let _ = epoll_fd
                .wait(&mut events, EpollTimeout::try_from(Duration::from_millis(10)).unwrap())
                .expect("Epoll wait failed");
            if events == [EpollEvent::new(EpollFlags::EPOLLIN, 1)] {
                // We can wake up from the timeout too. If we try to read the EventFD after waking
                // up from a timeout instead of a real event, we'll just block, so only call read()
                // if we got a real event.
                self.awaiting_job_event.read().unwrap();
            }

            // We have to do this outside of the loop to make sure we don't hold the lock (and a
            // reference to self).
            let mut possible_job = (*self.work_queue.lock().unwrap()).pop_front();
            while let Some(mut job) = possible_job {
                let is_empty_job = job.input.is_none();
                let drain = job.get_drain();
                let timestamp = job.timestamp;
                if !is_empty_job {
                    let frame_fourcc = job.input.as_ref().unwrap().fourcc();
                    let frame_y_stride = job.input.as_ref().unwrap().get_plane_pitch()[0];
                    let frame_y_size = job.input.as_ref().unwrap().get_plane_size()[0];
                    let can_import_frame = frame_y_stride == self.coded_resolution.width as usize
                        && frame_y_size >= self.coded_resolution.get_area()
                        && DecodedFormat::from(frame_fourcc) == REAL_INPUT_FORMAT;
                    let frame = if can_import_frame {
                        job.input.take().unwrap()
                    } else {
                        let mut tmp = match (*self.alloc_cb.lock().unwrap())() {
                            Some(tmp_frame) => tmp_frame,
                            None => {
                                // Try again when a temp frame is available.
                                (*self.work_queue.lock().unwrap()).push_front(job);
                                break;
                            }
                        };

                        if let Err(_) = convert_video_frame(job.input.as_ref().unwrap(), &mut tmp) {
                            log::debug!("Failed to copy input frame to properly aligned buffer!");
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                            break;
                        }
                        {
                            let tmp_mapping = tmp.map_mut().expect("Failed to map tmp frame!");
                            let tmp_planes = tmp_mapping.get();
                            extend_border_nv12(
                                *tmp_planes[Y_PLANE].borrow_mut(),
                                *tmp_planes[UV_PLANE].borrow_mut(),
                                self.visible_resolution.width as usize,
                                self.visible_resolution.height as usize,
                                self.coded_resolution.width as usize,
                                self.coded_resolution.height as usize,
                            );
                        }

                        tmp
                    };

                    let curr_bitrate = match self.current_tunings.rate_control {
                        ConstantBitrate(bitrate) => bitrate,
                        ConstantQuality(_) => {
                            log::debug!("CQ encoding not currently supported");
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                            break;
                        }
                    };
                    let new_framerate = job.framerate.load(Ordering::Relaxed);
                    if job.bitrate != curr_bitrate
                        || new_framerate != self.current_tunings.framerate
                    {
                        self.current_tunings.rate_control =
                            RateControl::ConstantBitrate(job.bitrate);
                        self.current_tunings.framerate = new_framerate;
                        #[cfg(feature = "ubc")]
                        {
                            self.bitrate_controller.tune(job.bitrate, new_framerate);
                        }
                        #[cfg(not(feature = "ubc"))]
                        {
                            if let Err(err) = self.encoder.tune(self.current_tunings.clone()) {
                                log::debug!("Error adjusting tunings! {:?}", err);
                                *self.state.0.lock().unwrap() = C2State::C2Error;
                                (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                                break;
                            }
                        }
                    };

                    #[cfg(feature = "ubc")]
                    {
                        // TODO: Add a field to the job for "force keyframe" instead of just
                        // hard-coding it to false.
                        let qp = self.bitrate_controller.get_qp(
                            self.current_tunings.min_quality,
                            self.current_tunings.max_quality,
                            false,
                        );
                        self.encoder.tune(Tunings {
                            rate_control: RateControl::ConstantQuality(qp),
                            framerate: self.current_tunings.framerate,
                            min_quality: self.current_tunings.min_quality,
                            max_quality: self.current_tunings.max_quality,
                        });
                    }

                    let meta = FrameMetadata {
                        timestamp: timestamp,
                        layout: Default::default(),
                        force_keyframe: false,
                    };
                    #[cfg(feature = "vaapi")]
                    let encode_result = self.encoder.encode(meta, frame);
                    #[cfg(feature = "v4l2")]
                    let encode_result = self.encoder.encode(meta, V4l2VideoFrame(frame));
                    match encode_result {
                        Ok(_) => self.in_flight_queue.push_back(job),
                        Err(err) => {
                            log::debug!("Error encoding frame! {:?}", err);
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                            break;
                        }
                    }
                }

                if drain != DrainMode::NoDrain {
                    if let Err(err) = self.encoder.drain() {
                        log::debug!("Error draining encoder! {:?}", err);
                        *self.state.0.lock().unwrap() = C2State::C2Error;
                        (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        break;
                    }
                }

                self.poll_complete_frames();

                // Return a C2Work item for explicit drain requests.
                if is_empty_job && drain != DrainMode::NoDrain && drain != DrainMode::SyntheticDrain
                {
                    // Note that drains flush all pending frames, so there should be nothing in the
                    // queue after the poll_complete_frames() call during a drain. Thus, the timestamp
                    // is correct without any additional logic.
                    (*self.work_done_cb.lock().unwrap())(C2EncodeJob {
                        timestamp: timestamp,
                        drain: drain,
                        ..Default::default()
                    });
                }

                possible_job = (*self.work_queue.lock().unwrap()).pop_front();
            }
        }
    }
}
