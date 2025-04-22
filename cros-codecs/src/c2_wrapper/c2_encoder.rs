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
use crate::DecodedFormat;
use crate::EncodedFormat;
use crate::Fourcc;
use crate::Resolution;

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
        output_fourcc: Fourcc,
        awaiting_job_event: Arc<EventFd>,
        error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
        work_done_cb: Arc<Mutex<dyn FnMut(C2EncodeJob<V>) + Send + 'static>>,
        work_queue: Arc<Mutex<VecDeque<C2EncodeJob<V>>>>,
        state: Arc<(Mutex<C2State>, Condvar)>,
        framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
        alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
        options: Self::Options,
    ) -> Result<Self, String> {
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

        let mut backend = B::new(options)?;
        let (encoder, visible_resolution, coded_resolution) =
            backend.get_encoder(REAL_INPUT_FORMAT, EncodedFormat::from(output_fourcc))?;
        (*framepool_hint_cb.lock().unwrap())(StreamInfo {
            format: REAL_INPUT_FORMAT,
            coded_resolution: coded_resolution.clone(),
            display_resolution: visible_resolution.clone(),
            min_num_frames: match EncodedFormat::from(output_fourcc) {
                // TODO: Chosen arbitrarily. Do we ever use more than one reference frame for H264?
                EncodedFormat::H264 | EncodedFormat::H265 => 4,
                EncodedFormat::VP8 => VP8_NUM_REF_FRAMES,
                EncodedFormat::VP9 => VP9_NUM_REF_FRAMES,
                EncodedFormat::AV1 => AV1_NUM_REF_FRAMES,
            } + IN_FLIGHT_FRAMES,
        });

        #[cfg(feature = "ubc")]
        {
            let (min_qp, max_qp) = match EncodedFormat::from(output_fourcc) {
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
                        #[cfg(feature = "ubc")]
                        {
                            self.bitrate_controller.tune(job.bitrate, new_framerate);
                        }
                        #[cfg(not(feature = "ubc"))]
                        {
                            self.current_tunings.rate_control =
                                RateControl::ConstantBitrate(job.bitrate);
                            self.current_tunings.framerate = new_framerate;

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
