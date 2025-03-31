// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
use crate::video_frame::auxiliary_video_frame::AuxiliaryVideoFrame;
use crate::video_frame::frame_pool::FramePool;
use crate::video_frame::frame_pool::PooledVideoFrame;
#[cfg(feature = "vaapi")]
use crate::video_frame::gbm_video_frame::{GbmDevice, GbmUsage, GbmVideoFrame};
#[cfg(feature = "v4l2")]
use crate::video_frame::v4l2_mmap_video_frame::V4l2MmapVideoFrame;
use crate::video_frame::VideoFrame;
use crate::EncodedFormat;
use crate::Fourcc;

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
    // TODO: Support stateful video decoders.
    fn get_decoder<V: VideoFrame + 'static>(
        &mut self,
        input_format: EncodedFormat,
    ) -> Result<DynStatelessVideoDecoder<V>, String>;
}

#[cfg(feature = "vaapi")]
type InternalAuxiliaryVideoFrame = GbmVideoFrame;
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
    state: Arc<Mutex<C2State>>,
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
    // Processes events from the decoder. Primarily these are frame decoded events and DRCs.
    fn check_events(&mut self) {
        loop {
            let stream_info = match &self.decoder {
                C2Decoder::ImportingDecoder(decoder) => decoder.stream_info().map(|x| x.clone()),
                C2Decoder::ConvertingDecoder(decoder) => decoder.stream_info().map(|x| x.clone()),
            };
            match &mut self.decoder {
                C2Decoder::ImportingDecoder(decoder) => match decoder.next_event() {
                    Some(DecoderEvent::FrameReady(ReadyFrame::CSD(timestamp))) => {
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            // CSD timestamps are already external.
                            timestamp: timestamp,
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FrameReady(ReadyFrame::Frame(frame))) => {
                        frame.sync().unwrap();
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            output: Some(frame.video_frame()),
                            timestamp: external_timestamp(frame.timestamp()),
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FormatChanged) => match stream_info {
                        Some(stream_info) => {
                            (*self.framepool_hint_cb.lock().unwrap())(stream_info.clone());
                        }
                        None => {
                            log::debug!("Could not get stream info after format change!");
                            *self.state.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                    },
                    _ => break,
                },
                C2Decoder::ConvertingDecoder(decoder) => match decoder.next_event() {
                    Some(DecoderEvent::FrameReady(ReadyFrame::CSD(timestamp))) => {
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            // CSD timestamps are already external.
                            timestamp: timestamp,
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
                            *self.state.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                            output: Some(Arc::new(dst_frame)),
                            timestamp: external_timestamp(frame.timestamp()),
                            ..Default::default()
                        });
                    }
                    Some(DecoderEvent::FormatChanged) => match stream_info {
                        Some(stream_info) => {
                            (*self.framepool_hint_cb.lock().unwrap())(stream_info.clone());
                            self.auxiliary_frame_pool.as_mut().unwrap().resize(&stream_info);
                        }
                        None => {
                            log::debug!("Could not get stream info after format change!");
                            *self.state.lock().unwrap() = C2State::C2Error;
                            (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        }
                    },
                    _ => break,
                },
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
        output_fourcc: Fourcc,
        awaiting_job_event: Arc<EventFd>,
        error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
        work_done_cb: Arc<Mutex<dyn FnMut(C2DecodeJob<V>) + Send + 'static>>,
        work_queue: Arc<Mutex<VecDeque<C2DecodeJob<V>>>>,
        state: Arc<Mutex<C2State>>,
        framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
        alloc_cb: Arc<Mutex<dyn FnMut() -> Option<V> + Send + 'static>>,
        options: Self::Options,
    ) -> Result<Self, String> {
        let mut backend = B::new(options)?;
        let backend_fourccs = backend.supported_output_formats(input_fourcc)?;
        let (auxiliary_frame_pool, decoder) = if backend_fourccs.contains(&output_fourcc) {
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

        while *self.state.lock().unwrap() == C2State::C2Running {
            // Poll for decoder events or pending job events.
            let mut events = [EpollEvent::empty()];
            let _nb_fds = self.epoll_fd.wait(&mut events, EpollTimeout::NONE).unwrap();

            if events == [EpollEvent::new(EpollFlags::EPOLLIN, 2)] {
                self.awaiting_job_event.read().unwrap();
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
                            &mut *self.alloc_cb.lock().unwrap(),
                        ),
                        C2Decoder::ConvertingDecoder(decoder) => {
                            decoder.decode(job.timestamp, bitstream, &mut || {
                                let external = (*self.alloc_cb.lock().unwrap())()?;
                                let internal =
                                    self.auxiliary_frame_pool.as_mut().unwrap().alloc()?;
                                Some(AuxiliaryVideoFrame {
                                    internal: internal,
                                    external: Mutex::new(Some(external)),
                                })
                            })
                        }
                    }
                } else {
                    // The drain signals are artificial jobs constructed by the C2Wrapper itself,
                    // so we don't want to return C2Work objects for them.
                    Ok((0, job.get_drain() != DrainMode::NoDrain))
                };
                match decode_result {
                    Ok((num_bytes, processed_visible_frame)) => {
                        job.contains_visible_frame |= processed_visible_frame;
                        if num_bytes != job.input.len() {
                            job.input = (&job.input[num_bytes..]).to_vec();
                            (*self.work_queue.lock().unwrap()).push_front(job);
                        } else {
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

                            if job.get_drain() != DrainMode::NoDrain {
                                let flush_result = match &mut self.decoder {
                                    C2Decoder::ImportingDecoder(decoder) => decoder.flush(),
                                    C2Decoder::ConvertingDecoder(decoder) => decoder.flush(),
                                };
                                if let Err(_) = flush_result {
                                    log::debug!("Error handling drain request!");
                                    *self.state.lock().unwrap() = C2State::C2Error;
                                    (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                                } else {
                                    self.check_events();
                                    if job.get_drain() == DrainMode::EOSDrain {
                                        (*self.work_done_cb.lock().unwrap())(C2DecodeJob {
                                            timestamp: job.timestamp,
                                            drain: DrainMode::EOSDrain,
                                            ..Default::default()
                                        });
                                    }
                                }
                                break;
                            }
                        }
                    }
                    Err(DecodeError::NotEnoughOutputBuffers(_) | DecodeError::CheckEvents) => {
                        (*self.work_queue.lock().unwrap()).push_front(job);
                        break;
                    }
                    Err(e) => {
                        log::debug!("Unhandled error message from decoder {e:?}");
                        *self.state.lock().unwrap() = C2State::C2Error;
                        (*self.error_cb.lock().unwrap())(C2Status::C2BadValue);
                        break;
                    }
                }
                possible_job = (*self.work_queue.lock().unwrap()).pop_front();
            }
            self.check_events();
        }
    }
}
