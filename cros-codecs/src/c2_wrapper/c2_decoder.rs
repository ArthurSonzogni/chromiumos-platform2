// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use drm_fourcc::DrmModifier;
use nix::errno::Errno;
use nix::sys::epoll::Epoll;
use nix::sys::epoll::EpollCreateFlags;
use nix::sys::epoll::EpollEvent;
use nix::sys::epoll::EpollFlags;
use nix::sys::epoll::EpollTimeout;
use nix::sys::eventfd::EventFd;

use std::collections::VecDeque;
use std::marker::PhantomData;
use std::os::fd::AsFd;
#[cfg(feature = "vaapi")]
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Condvar;
use std::sync::Mutex;
use thiserror::Error;

use crate::c2_wrapper::C2DecodeJob;
use crate::c2_wrapper::C2State;
use crate::c2_wrapper::C2Status;
use crate::c2_wrapper::C2Worker;
use crate::c2_wrapper::DrainMode;
use crate::c2_wrapper::Job;
use crate::decoder::stateless::DecodeError;
use crate::decoder::stateless::DynStatelessVideoDecoder;
use crate::decoder::DecoderEvent;
use crate::decoder::ReadyFrame;
use crate::decoder::StreamInfo;
use crate::image_processing::convert_video_frame;
use crate::image_processing::detile_y_tile;
use crate::image_processing::modifier_conversion;
use crate::image_processing::{Y_TILE_HEIGHT, Y_TILE_WIDTH};
use crate::utils::align_up;
use crate::video_frame::auxiliary_video_frame::AuxiliaryVideoFrame;
use crate::video_frame::frame_pool::FramePool;
use crate::video_frame::frame_pool::PooledVideoFrame;
#[cfg(feature = "vaapi")]
use crate::video_frame::gbm_video_frame::{GbmDevice, GbmUsage, GbmVideoFrame};
#[cfg(feature = "vaapi")]
use crate::video_frame::generic_dma_video_frame::GenericDmaVideoFrame;
#[cfg(feature = "v4l2")]
use crate::video_frame::v4l2_mmap_video_frame::V4l2MmapVideoFrame;
use crate::video_frame::VideoFrame;
use crate::DecodedFormat;
use crate::EncodedFormat;
use crate::Fourcc;
use crate::Resolution;

#[derive(Debug, Error)]
pub enum C2DecoderPollErrorWrapper {
    #[error("failed to create Epoll: {0}")]
    Epoll(Errno),
    #[error("failed to add poll FDs to Epoll: {0}")]
    EpollAdd(Errno),
}

pub trait C2DecoderBackend {
    type DecoderOptions: Clone + Send + 'static;

    fn new(options: Self::DecoderOptions) -> Result<Self, String>
    where
        Self: Sized;
    fn supported_output_formats(&self, fourcc: Fourcc) -> Result<Vec<Fourcc>, String>;
    fn modifier(&self) -> u64;
    // TODO: Support stateful video decoders.
    fn get_decoder<V: VideoFrame + 'static>(
        &mut self,
        input_format: EncodedFormat,
    ) -> Result<DynStatelessVideoDecoder<V>, String>;
}

// TODO(b/416546169): Switch this back to GbmVideoFrame once b/416546169 is fixed.
#[cfg(feature = "vaapi")]
type InternalAuxiliaryVideoFrame = GenericDmaVideoFrame<()>;
#[cfg(feature = "v4l2")]
type InternalAuxiliaryVideoFrame = V4l2MmapVideoFrame;

// An "importing decoder" can directly import the DMA bufs we are getting, while a "converting
// decoder" is used for performing image processing routines to convert between the video hardware
// output and a pixel format that can be consumed by the GPU and display controller.
// TODO: Come up with a better name for these?
enum C2Decoder<V: VideoFrame> {
    ImportingDecoder(DynStatelessVideoDecoder<V>),
    ConvertingDecoder(
        DynStatelessVideoDecoder<
            AuxiliaryVideoFrame<PooledVideoFrame<InternalAuxiliaryVideoFrame>, V>,
        >,
    ),
}

pub struct C2DecoderWorker<V, B>
where
    V: VideoFrame,
    B: C2DecoderBackend,
{
    decoder: C2Decoder<V>,
    epoll_fd: Epoll,
    awaiting_job_event: Arc<EventFd>,
    auxiliary_frame_pool: Option<FramePool<InternalAuxiliaryVideoFrame>>,
    error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
    work_done_cb: Arc<Mutex<dyn FnMut(C2DecodeJob<V>) + Send + 'static>>,
    framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
    alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
    work_queue: Arc<Mutex<VecDeque<C2DecodeJob<V>>>>,
    state: Arc<(Mutex<C2State>, Condvar)>,
    drain_status: Option<(u64, DrainMode)>,
    // Keep track of what DRM modifier the decoder expects. At least in VA-API, the driver will
    // import a DMA buffer of any modifier type and simply ignore the given modifier entirely.
    decoder_modifier: u64,
    // Keep track of what DRM modifier the client expects. This can sometimes change during
    // detached surface mode, so we just cache the modifier of the first test alloc. Note that we
    // only distinguish between linear and non-linear client modifiers so we can be forward
    // compatible with future driver versions that may support other types of tiling modifiers.
    client_linear_modifier: bool,
    // If we need to "convert" the DRM modifier, we don't need to allocate a full auxiliary frame
    // pool because the existing frame pool already gives us frames with the right size, format,
    // and layout.
    // TODO: Should we use this same technique in place of AuxiliaryVideoFrame? We should really
    // only ever need 1 frame in the output frame pool for the ConvertingDecoder since we manage
    // all the reference frames internally. We could save some memory this way, which might be
    // especially important for L1.
    scratch_frame: Option<V>,
    _phantom: PhantomData<B>,
}

fn external_timestamp(internal_timestamp: u64) -> u64 {
    #[cfg(feature = "vaapi")]
    {
        internal_timestamp
    }
    #[cfg(feature = "v4l2")]
    {
        // We have a hack on V4L2 for VP9 and AV1 where we use the LSB to deduplicate the hidden frame
        // of a superframe.
        internal_timestamp >> 1
    }
}

impl<V, B> C2DecoderWorker<V, B>
where
    V: VideoFrame,
    B: C2DecoderBackend,
{
    // Helper function for correctly populating the |drain| field of the C2DecodeJob objects we
    // return to the HAL.
    fn get_drain_status(&mut self, timestamp: u64) -> DrainMode {
        if let Some((drain_timestamp, drain_mode)) = self.drain_status.take() {
            if drain_timestamp == timestamp {
                drain_mode
            } else {
                self.drain_status = Some((drain_timestamp, drain_mode));
                DrainMode::NoDrain
            }
        } else {
            DrainMode::NoDrain
        }
    }

    // Processes events from the decoder. Primarily these are frame decoded events and DRCs.
    fn check_events(&mut self) {
        loop {
            let mut scratch_frame = if let Some(frame) = self.scratch_frame.take() {
                Some(frame)
            } else if self.client_linear_modifier {
                return;
            } else {
                None
            };

            let stream_info = match &self.decoder {
                C2Decoder::ImportingDecoder(decoder) => decoder.stream_info().map(|x| x.clone()),
                C2Decoder::ConvertingDecoder(decoder) => decoder.stream_info().map(|x| x.clone()),
            };
            match &mut self.decoder {
                C2Decoder::ImportingDecoder(decoder) => match decoder.next_event() {
                    Some(DecoderEvent::FrameReady(ReadyFrame::CSD(timestamp))) => {
                        let drain = self.get_drain_status(timestamp);
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            // CSD timestamps are already external.
                            timestamp: timestamp,
                            drain: drain,
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FrameReady(ReadyFrame::Frame(frame))) => {
                        frame.sync().unwrap();
                        let timestamp = external_timestamp(frame.timestamp());
                        let drain = self.get_drain_status(timestamp);
                        let mut frame = frame.video_frame();
                        if self.client_linear_modifier
                            && scratch_frame.as_ref().unwrap().modifier()
                                == DrmModifier::Linear.into()
                        {
                            if let Err(msg) = modifier_conversion(
                                &*frame,
                                self.decoder_modifier,
                                scratch_frame.as_mut().unwrap(),
                                DrmModifier::Linear.into(),
                            ) {
                                log::debug!("Error converting modifiers: {}", msg);
                                *self.state.0.lock().unwrap() = C2State::C2Error;
                                (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                                return;
                            }
                            frame = Arc::new(scratch_frame.unwrap());
                        }
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            output: Some(frame),
                            timestamp: timestamp,
                            drain: drain,
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FormatChanged) => match stream_info {
                        Some(stream_info) => {
                            if self.decoder_modifier == DrmModifier::I915_y_tiled.into() {
                                // Sometimes gralloc will give us linear buffers with incorrect
                                // alignment, so we leverage coded_size to make sure the alignment
                                // is appropriate for the decoder's tiling scheme. Note that
                                // because the chroma plane is subsampled, we double the alignment
                                // requirement.
                                // TODO: We may have to change this to support other tiling schemes.
                                (*self.framepool_hint_cb.lock().unwrap())(StreamInfo {
                                    format: stream_info.format,
                                    coded_resolution: Resolution {
                                        width: align_up(
                                            stream_info.coded_resolution.width,
                                            2 * Y_TILE_WIDTH as u32,
                                        ),
                                        height: align_up(
                                            stream_info.coded_resolution.height,
                                            2 * Y_TILE_HEIGHT as u32,
                                        ),
                                    },
                                    display_resolution: stream_info.display_resolution.clone(),
                                    min_num_frames: stream_info.min_num_frames + 1,
                                    range: stream_info.range,
                                    primaries: stream_info.primaries,
                                    transfer: stream_info.transfer,
                                    matrix: stream_info.matrix,
                                });
                            } else {
                                (*self.framepool_hint_cb.lock().unwrap())(stream_info.clone());
                            }

                            self.scratch_frame = None;

                            // Retry the frame that caused the format change.
                            self.awaiting_job_event.write(1).unwrap();

                            return;
                        }
                        None => {
                            log::debug!("Could not get stream info after format change!");
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                    },
                    _ => break,
                },
                C2Decoder::ConvertingDecoder(decoder) => match decoder.next_event() {
                    Some(DecoderEvent::FrameReady(ReadyFrame::CSD(timestamp))) => {
                        let drain = self.get_drain_status(timestamp);
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            // CSD timestamps are already external.
                            timestamp: timestamp,
                            drain: drain,
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FrameReady(ReadyFrame::Frame(frame))) => {
                        frame.sync().unwrap();
                        let aux_frame = &*frame.video_frame();
                        let mut dst_frame = (*aux_frame.external.lock().unwrap())
                            .take()
                            .expect("Received the same auxiliary frame twice!");
                        let src_frame = &aux_frame.internal;
                        if let Err(err) = convert_video_frame(src_frame, &mut dst_frame) {
                            log::debug!("Error converting VideoFrame! {err}");
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                        let timestamp = external_timestamp(frame.timestamp());
                        let drain = self.get_drain_status(timestamp);
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            output: Some(Arc::new(dst_frame)),
                            timestamp: timestamp,
                            drain: drain,
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FormatChanged) => match stream_info {
                        Some(stream_info) => {
                            (*self.framepool_hint_cb.lock().unwrap())(stream_info.clone());
                            self.auxiliary_frame_pool.as_mut().unwrap().resize(&stream_info);
                            // Retry the frame that caused the format change.
                            self.awaiting_job_event.write(1).unwrap();
                        }
                        None => {
                            log::debug!("Could not get stream info after format change!");
                            *self.state.0.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                    },
                    _ => break,
                },
            }

            if self.client_linear_modifier {
                self.scratch_frame = (*self.alloc_cb.lock().unwrap())();
            }
        }
    }
}

impl<V, B> C2Worker<C2DecodeJob<V>> for C2DecoderWorker<V, B>
where
    V: VideoFrame,
    B: C2DecoderBackend,
{
    type Options = <B as C2DecoderBackend>::DecoderOptions;

    fn new(
        input_fourcc: Fourcc,
        output_fourccs: Vec<Fourcc>,
        awaiting_job_event: Arc<EventFd>,
        error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
        work_done_cb: Arc<Mutex<dyn FnMut(C2DecodeJob<V>) + Send + 'static>>,
        work_queue: Arc<Mutex<VecDeque<C2DecodeJob<V>>>>,
        state: Arc<(Mutex<C2State>, Condvar)>,
        framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
        alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
        options: Self::Options,
    ) -> Result<Self, String> {
        let mut backend = B::new(options)?;
        let backend_fourccs = backend.supported_output_formats(input_fourcc)?;
        let decoder_modifier = backend.modifier();
        let mut client_linear_modifier = false;

        let can_import = output_fourccs
            .iter()
            .fold(true, |acc, fourcc| -> bool { acc & backend_fourccs.contains(fourcc) });
        let (auxiliary_frame_pool, decoder) = if can_import {
            // Perform a test alloc to determine what DRM modifiers the client expects of us.
            // 256x256 was chosen because it was thought to maximize compatibility with various
            // alignment constraints.
            (*framepool_hint_cb.lock().unwrap())(StreamInfo {
                format: DecodedFormat::from(output_fourccs[0]),
                coded_resolution: Resolution { width: 256, height: 256 },
                display_resolution: Resolution { width: 256, height: 256 },
                min_num_frames: 1,
                range: Default::default(),
                primaries: Default::default(),
                transfer: Default::default(),
                matrix: Default::default(),
            });
            let test_alloc = (*alloc_cb.lock().unwrap())()
                .ok_or("Failed to perform test allocation!".to_string())?;
            client_linear_modifier = test_alloc.modifier() == DrmModifier::Linear.into();

            (
                None,
                C2Decoder::ImportingDecoder(
                    backend.get_decoder(EncodedFormat::from(input_fourcc))?,
                ),
            )
        } else {
            #[cfg(feature = "vaapi")]
            {
                let gbm_device = Arc::new(
                    GbmDevice::open(PathBuf::from("/dev/dri/renderD128"))
                        .expect("Could not open GBM device!"),
                );
                let framepool = FramePool::new(move |stream_info: &StreamInfo| {
                    // TODO: Query the driver for these alignment params.
                    <Arc<GbmDevice> as Clone>::clone(&gbm_device)
                        .new_frame(
                            Fourcc::from(stream_info.format),
                            stream_info.display_resolution.clone(),
                            stream_info.coded_resolution.clone(),
                            GbmUsage::Decode,
                        )
                        .expect("Could not allocate frame for auxiliary frame pool!")
                        .to_generic_dma_video_frame()
                        .expect("Could not export GBM frame to DMA frame!")
                });
                (
                    Some(framepool),
                    C2Decoder::ConvertingDecoder(
                        backend.get_decoder(EncodedFormat::from(input_fourcc))?,
                    ),
                )
            }
            #[cfg(feature = "v4l2")]
            {
                let framepool = FramePool::new(move |stream_info: &StreamInfo| {
                    V4l2MmapVideoFrame::new(
                        Fourcc::from(stream_info.format),
                        stream_info.display_resolution.clone(),
                    )
                });
                (
                    Some(framepool),
                    C2Decoder::ConvertingDecoder(
                        backend.get_decoder(EncodedFormat::from(input_fourcc))?,
                    ),
                )
            }
        };
        Ok(Self {
            decoder: decoder,
            auxiliary_frame_pool: auxiliary_frame_pool,
            epoll_fd: Epoll::new(EpollCreateFlags::empty())
                .map_err(C2DecoderPollErrorWrapper::Epoll)
                .unwrap(),
            awaiting_job_event: awaiting_job_event,
            error_cb: error_cb,
            work_done_cb: work_done_cb,
            framepool_hint_cb: framepool_hint_cb,
            alloc_cb: alloc_cb,
            work_queue: work_queue,
            state: state,
            drain_status: None,
            decoder_modifier: decoder_modifier,
            client_linear_modifier: client_linear_modifier,
            scratch_frame: None,
            _phantom: Default::default(),
        })
    }

    fn process_loop(&mut self) {
        self.epoll_fd = Epoll::new(EpollCreateFlags::empty())
            .map_err(C2DecoderPollErrorWrapper::Epoll)
            .unwrap();
        let _ = self
            .epoll_fd
            .add(
                match &self.decoder {
                    C2Decoder::ImportingDecoder(decoder) => decoder.poll_fd(),
                    C2Decoder::ConvertingDecoder(decoder) => decoder.poll_fd(),
                },
                EpollEvent::new(EpollFlags::EPOLLIN, 1),
            )
            .map_err(C2DecoderPollErrorWrapper::EpollAdd);
        self.epoll_fd
            .add(self.awaiting_job_event.as_fd(), EpollEvent::new(EpollFlags::EPOLLIN, 2))
            .map_err(C2DecoderPollErrorWrapper::EpollAdd)
            .unwrap();

        while *self.state.0.lock().unwrap() == C2State::C2Running {
            // Poll for decoder events or pending job events.
            let mut events = [EpollEvent::empty()];
            let _nb_fds = self.epoll_fd.wait(&mut events, EpollTimeout::NONE).unwrap();

            if events == [EpollEvent::new(EpollFlags::EPOLLIN, 2)] {
                self.awaiting_job_event.read().unwrap();
            }

            if self.client_linear_modifier && self.scratch_frame.is_none() {
                self.scratch_frame = (*self.alloc_cb.lock().unwrap())();
                if self.scratch_frame.is_none() {
                    self.check_events();
                    continue;
                }
            }

            // We want to try sending compressed buffers to the decoder regardless of what event
            // woke us up, because we either have new work, or we might more output buffers
            // available.
            let mut possible_job = (*self.work_queue.lock().unwrap()).pop_front();
            while let Some(mut job) = possible_job {
                let bitstream = job.input.as_slice();
                let decode_result = if !job.input.is_empty() {
                    match &mut self.decoder {
                        C2Decoder::ImportingDecoder(decoder) => decoder.decode(
                            job.timestamp,
                            bitstream,
                            job.codec_specific_data,
                            &mut *self.alloc_cb.lock().unwrap(),
                        ),
                        C2Decoder::ConvertingDecoder(decoder) => decoder.decode(
                            job.timestamp,
                            bitstream,
                            job.codec_specific_data,
                            &mut || {
                                let external = (*self.alloc_cb.lock().unwrap())()?;
                                let internal =
                                    self.auxiliary_frame_pool.as_mut().unwrap().alloc()?;
                                Some(AuxiliaryVideoFrame {
                                    internal: internal,
                                    external: Mutex::new(Some(external)),
                                })
                            },
                        ),
                    }
                } else {
                    // The drain signals are artificial jobs constructed by the C2Wrapper itself,
                    // so we don't want to return C2Work objects for them.
                    Ok((0, job.get_drain() == DrainMode::SyntheticDrain))
                };
                match decode_result {
                    Ok((num_bytes, processed_visible_frame)) => {
                        job.contains_visible_frame |= processed_visible_frame;
                        if num_bytes != job.input.len() {
                            job.input = (&job.input[num_bytes..]).to_vec();
                            (*self.work_queue.lock().unwrap()).push_front(job);
                        } else {
                            if job.get_drain() != DrainMode::NoDrain {
                                let flush_result = match &mut self.decoder {
                                    C2Decoder::ImportingDecoder(decoder) => decoder.flush(),
                                    C2Decoder::ConvertingDecoder(decoder) => decoder.flush(),
                                };
                                self.drain_status = Some((job.timestamp, job.get_drain()));
                                if let Err(_) = flush_result {
                                    log::debug!("Error handling drain request!");
                                    *self.state.0.lock().unwrap() = C2State::C2Error;
                                    (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                                } else {
                                    self.check_events();
                                }
                            }

                            if !job.contains_visible_frame {
                                match &mut self.decoder {
                                    C2Decoder::ImportingDecoder(decoder) => {
                                        decoder.queue_empty_frame(job.timestamp)
                                    }
                                    C2Decoder::ConvertingDecoder(decoder) => {
                                        decoder.queue_empty_frame(job.timestamp)
                                    }
                                };
                            }
                        }
                    }
                    Err(DecodeError::NotEnoughOutputBuffers(_) | DecodeError::CheckEvents) => {
                        (*self.work_queue.lock().unwrap()).push_front(job);
                        break;
                    }
                    Err(e) => {
                        log::debug!("Unhandled error message from decoder {e:?}");
                        *self.state.0.lock().unwrap() = C2State::C2Error;
                        (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        break;
                    }
                }
                possible_job = (*self.work_queue.lock().unwrap()).pop_front();
            }
            self.check_events();
        }

        self.scratch_frame = None;
    }
}
