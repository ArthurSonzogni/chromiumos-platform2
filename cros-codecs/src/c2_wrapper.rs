// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use nix::errno::Errno;
use nix::sys::eventfd::EfdFlags;
use nix::sys::eventfd::EventFd;

use thiserror::Error;

use std::collections::VecDeque;
use std::marker::PhantomData;
use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::sync::Condvar;
use std::sync::Mutex;
use std::thread;
use std::thread::JoinHandle;
use std::vec::Vec;

use crate::decoder::StreamInfo;
use crate::video_frame::VideoFrame;
use crate::Fourcc;

pub mod c2_decoder;
pub mod c2_encoder;
#[cfg(feature = "v4l2")]
pub mod c2_v4l2_decoder;
#[cfg(feature = "v4l2")]
pub mod c2_v4l2_encoder;
#[cfg(feature = "vaapi")]
pub mod c2_vaapi_decoder;
#[cfg(feature = "vaapi")]
pub mod c2_vaapi_encoder;

#[derive(Debug, Default, PartialEq, Eq, Copy, Clone)]
pub enum DrainMode {
    // Not draining
    #[default]
    NoDrain = -1,
    // Drain the C2 component and signal an EOS. Currently we also change the state to stop.
    EOSDrain = 0,
    // Drain the C2 component, but keep accepting new jobs in the queue immediately after.
    NoEOSDrain = 1,
    // Drain signal coming from a drain() or flush() call. These are distinct because we should
    // not return C2Work items for these.
    SyntheticDrain = 2,
}

#[derive(Debug)]
pub struct C2DecodeJob<V: VideoFrame> {
    // Compressed input data
    // TODO: Use VideoFrame for input too
    pub input: Vec<u8>,
    // Decompressed output frame. Note that this needs to be reference counted because we may still
    // use this frame as a reference frame even while we're displaying it.
    pub output: Option<Arc<V>>,
    pub timestamp: u64,
    pub drain: DrainMode,
    // Keeps track of whether or not the corresponding C2Work item contains actual frame data.
    // Codec2 will expect the C2Work item back regardless, so we will need to send back an empty
    // C2Work with the correct timestamp if this is false. Note that this field is populated by the
    // C2DecoderWorker based on parsing, so we don't actually need to populate this at the HAL
    // level.
    pub contains_visible_frame: bool,
    pub codec_specific_data: bool,
    // TODO: Add output delay and color aspect support as needed.
}

impl<V> Job for C2DecodeJob<V>
where
    V: VideoFrame,
{
    type Frame = V;

    fn set_drain(&mut self, drain: DrainMode) {
        self.drain = drain;
    }

    fn get_drain(&self) -> DrainMode {
        self.drain
    }
}

impl<V: VideoFrame> Default for C2DecodeJob<V> {
    fn default() -> Self {
        Self {
            input: vec![],
            output: None,
            timestamp: 0,
            contains_visible_frame: false,
            drain: DrainMode::NoDrain,
            codec_specific_data: false,
        }
    }
}

pub trait Job: Send + 'static {
    type Frame: VideoFrame;

    fn set_drain(&mut self, drain: DrainMode) -> ();
    fn get_drain(&self) -> DrainMode;
}

#[derive(Debug)]
pub struct C2EncodeJob<V: VideoFrame> {
    pub input: Option<V>,
    // TODO: Use VideoFrame for output too
    pub output: Vec<u8>,
    // Codec-specific data to output as part of this C2EncodeJob. This is intended to be used in the
    // H.264 case as "initialization data" for the client: the first C2EncodeJob to be sent to the
    // client should contain the concatenation of the SPS and PPS NALUs separated by start codes as
    // follows:
    //
    //     [0x0, 0x0, 0x0, 0x1, ... SPS ..., 0x0, 0x0, 0x0, 0x1, ... PPS ...]
    //
    // On the C++ HAL side, this CSD, if not empty, is expected to get copied to a
    // C2StreamInitDataInfo::output parameter.
    pub csd: Vec<u8>,
    // In microseconds.
    pub timestamp: u64,
    // TODO: only support CBR right now, follow up with VBR support.
    pub bitrate: u64,
    // Framerate is actually negotiated, so the encoder can change this value
    // based on the timestamps of the frames it receives.
    pub framerate: Arc<AtomicU32>,
    pub drain: DrainMode,
}

impl<V: VideoFrame> Default for C2EncodeJob<V> {
    fn default() -> Self {
        Self {
            input: None,
            output: vec![],
            csd: vec![],
            timestamp: 0,
            bitrate: 0,
            framerate: Arc::new(AtomicU32::new(0)),
            drain: DrainMode::NoDrain,
        }
    }
}

impl<V> Job for C2EncodeJob<V>
where
    V: VideoFrame,
{
    type Frame = V;

    fn set_drain(&mut self, drain: DrainMode) {
        self.drain = drain;
    }

    fn get_drain(&self) -> DrainMode {
        self.drain
    }
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum C2State {
    C2Running,
    C2Stopping,
    C2Stopped,
    // Note that on state C2Error, stop() must be called before we can start()
    // again.
    C2Error,
    C2Release,
}

// This is not a very "Rust-y" way of doing error handling, but it will
// hopefully make the FFI easier to write. Numerical values taken from
// frameworks/av/media/codec2/core/include/C2.h
// TODO: Improve error handling by adding more statuses.
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum C2Status {
    C2Ok = 0,
    C2BadState = 1,  // EPERM
    C2BadValue = 22, // EINVAL
}

// J should be either C2DecodeJob or C2EncodeJob.
pub trait C2Worker<J>
where
    J: Send + Job + 'static,
{
    type Options: Clone + Send + 'static;

    fn new(
        input_fourcc: Fourcc,
        output_fourccs: Vec<Fourcc>,
        awaiting_job_event: Arc<EventFd>,
        error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
        work_done_cb: Arc<Mutex<dyn FnMut(J) + Send + 'static>>,
        work_queue: Arc<Mutex<VecDeque<J>>>,
        state: Arc<(Mutex<C2State>, Condvar)>,
        framepool_hint_cb: Arc<Mutex<dyn FnMut(StreamInfo) + Send + 'static>>,
        alloc_cb: Arc<Mutex<dyn FnMut() -> Option<<J as Job>::Frame> + Send + 'static>>,
        options: Self::Options,
    ) -> Result<Self, String>
    where
        Self: Sized;

    fn process_loop(&mut self);
}

#[derive(Debug, Error)]
pub enum C2WrapperError {
    #[error("failed to create EventFd for awaiting job event: {0}")]
    AwaitingJobEventFd(Errno),
}

// Note that we do not guarantee thread safety in C2CrosCodecsWrapper.
pub struct C2Wrapper<J, W>
where
    J: Send + Default + Job + 'static,
    W: C2Worker<J>,
{
    awaiting_job_event: Arc<EventFd>,
    error_cb: Arc<Mutex<dyn FnMut(C2Status) + Send + 'static>>,
    work_queue: Arc<Mutex<VecDeque<J>>>,
    state: Arc<(Mutex<C2State>, Condvar)>,
    // This isn't actually optional, but we want to join this handle in drop(), but because drop()
    // takes an &mut self, we can't actually take ownership of this variable. So we workaround this
    // by just making it an optional and swapping it with None in drop().
    worker_thread: Option<JoinHandle<()>>,
    // The instance of W actually lives in the thread creation closure, not
    // this struct. We use "fn() -> W" for this type signature instead of just regular "W" as a
    // workaround to make sure this PhantomData doesn't affect the Send and Sync properties of the
    // overall C2Wrapper.
    _phantom: PhantomData<fn() -> W>,
}

impl<J, W> C2Wrapper<J, W>
where
    J: Send + Default + Job + 'static,
    W: C2Worker<J>,
{
    pub fn new(
        input_fourcc: Fourcc,
        // List of output Fourccs that must be supported. For decoders, this should include 1
        // output format for each bit depth supported. For encoders, this list should only ever
        // have one element.
        output_fourccs: Vec<Fourcc>,
        error_cb: impl FnMut(C2Status) + Send + 'static,
        work_done_cb: impl FnMut(J) + Send + 'static,
        framepool_hint_cb: impl FnMut(StreamInfo) + Send + 'static,
        alloc_cb: impl FnMut() -> Option<<J as Job>::Frame> + Send + 'static,
        options: <W as C2Worker<J>>::Options,
    ) -> Self {
        let awaiting_job_event = Arc::new(
            EventFd::from_flags(EfdFlags::EFD_SEMAPHORE)
                .map_err(C2WrapperError::AwaitingJobEventFd)
                .unwrap(),
        );
        let awaiting_job_event_clone = awaiting_job_event.clone();
        let error_cb = Arc::new(Mutex::new(error_cb));
        let error_cb_clone = error_cb.clone();
        let work_done_cb = Arc::new(Mutex::new(work_done_cb));
        let work_queue: Arc<Mutex<VecDeque<J>>> = Arc::new(Mutex::new(VecDeque::new()));
        let work_queue_clone = work_queue.clone();
        let state = Arc::new((Mutex::new(C2State::C2Stopped), Condvar::new()));
        let state_clone = state.clone();
        let framepool_hint_cb = Arc::new(Mutex::new(framepool_hint_cb));
        let alloc_cb = Arc::new(Mutex::new(alloc_cb));
        let worker_thread = Some(thread::spawn(move || {
            let (state_lock, state_cvar) = &*state_clone;
            let mut state = state_lock.lock().expect("Could not lock state");
            while *state != C2State::C2Release {
                if *state == C2State::C2Running {
                    // Otherwise we will just hold the lock during the processing loop, which will
                    // cause a deadlock.
                    drop(state);

                    let worker = W::new(
                        input_fourcc.clone(),
                        output_fourccs.clone(),
                        awaiting_job_event_clone.clone(),
                        error_cb_clone.clone(),
                        work_done_cb.clone(),
                        work_queue_clone.clone(),
                        state_clone.clone(),
                        framepool_hint_cb.clone(),
                        alloc_cb.clone(),
                        options.clone(),
                    );
                    match worker {
                        Ok(mut worker) => {
                            worker.process_loop();

                            // Note that we only lock the state again after the process loop exits.
                            state = state_lock.lock().expect("Could not lock state");
                            *state = C2State::C2Stopped;
                            state_cvar.notify_one();
                        }
                        Err(msg) => {
                            log::debug!("Error instantiating C2Worker {}", msg);
                            state = state_lock.lock().expect("Could not lock state");
                            *state = C2State::C2Error;
                            state_cvar.notify_one();
                            (*error_cb_clone.lock().unwrap())(C2Status::C2BadValue);
                        }
                    };
                } else {
                    // This is needed to handle the circumstance in which we call reset() after an
                    // error. The state will be C2Error, not C2Running, so we can't rely on the
                    // above logic to process the stop request.
                    if *state == C2State::C2Stopping {
                        *state = C2State::C2Stopped;
                        state_cvar.notify_one();
                    }

                    // It's important that this wait() call goes here, after the check for
                    // C2Running. Otherwise the call to start() might be executed before we fully
                    // initialize this thread. Because notify_one() doesn't do any kind of
                    // buffering, we can miss our "wake-up call" and just wait indefinitely.
                    state = state_cvar.wait(state).unwrap();
                }
            }
        }));

        Self {
            awaiting_job_event: awaiting_job_event,
            error_cb,
            work_queue,
            state,
            worker_thread,
            _phantom: Default::default(),
        }
    }

    // Start the decoder/encoder.
    // State will be C2Running after this call.
    pub fn start(&mut self) -> C2Status {
        let (state_lock, state_cvar) = &*self.state;
        {
            let mut state = state_lock.lock().expect("Could not lock state");
            if *state != C2State::C2Stopped {
                (*self.error_cb.lock().unwrap())(C2Status::C2BadState);
                return C2Status::C2BadState;
            }
            *state = C2State::C2Running;
            state_cvar.notify_one();
        }

        C2Status::C2Ok
    }

    // Helper method for stop() and reset() to re-use code: if `is_reset` is
    // true, no state validation is performed (suitable for reset()), otherwise
    // we validate that we're in the C2Running state (suitable for stop()). This
    // is necessary to abide by the C2Component API.
    fn stop_internal(&mut self, is_reset: bool) -> C2Status {
        let (state_lock, state_cvar) = &*self.state;
        {
            let mut state = state_lock.lock().expect("Could not lock state");
            if !is_reset && *state != C2State::C2Running {
                (*self.error_cb.lock().unwrap())(C2Status::C2BadState);
                return C2Status::C2BadState;
            }
            *state = C2State::C2Stopping;
            state_cvar.notify_one();
        }

        self.work_queue.lock().unwrap().drain(..);

        self.awaiting_job_event.write(1).unwrap();

        let mut state = state_lock.lock().expect("Could not lock state");
        while *state == C2State::C2Stopping {
            state = state_cvar.wait(state).unwrap();
        }

        C2Status::C2Ok
    }

    // Stop the decoder/encoder and abandon in-flight work.
    // Note that in event of error, stop() must be called before we can start()
    // again. This is to ensure we clear out the work queue.
    // State will be C2Stopped after this call.
    pub fn stop(&mut self) -> C2Status {
        self.stop_internal(/*is_reset=*/ false)
    }

    // Reset the decoder/encoder and abandon in-flight work.
    // For our purposes, this is equivalent to stop() except for the fact that
    // this method doesn't fail if the state is already C2Stopped.
    // State will be C2Stopped after this call.
    pub fn reset(&mut self) -> C2Status {
        self.stop_internal(/*is_reset=*/ true)
    }

    // Add work to the work queue.
    // State must be C2Running or this function is invalid.
    // State will remain C2Running.
    pub fn queue(&mut self, work_items: Vec<J>) -> C2Status {
        if *self.state.0.lock().expect("Could not lock state") != C2State::C2Running {
            (*self.error_cb.lock().unwrap())(C2Status::C2BadState);
            return C2Status::C2BadState;
        }

        self.work_queue.lock().unwrap().extend(work_items.into_iter());

        self.awaiting_job_event.write(1).unwrap();

        C2Status::C2Ok
    }

    // Flush work from the queue and return it as |flushed_work|.
    // State will not change after this call.
    // TODO: Support different flush modes.
    pub fn flush(&mut self, flushed_work: &mut Vec<J>) -> C2Status {
        if *self.state.0.lock().expect("Could not lock state") != C2State::C2Running {
            (*self.error_cb.lock().unwrap())(C2Status::C2BadState);
            return C2Status::C2BadState;
        }

        {
            let mut work_queue = self.work_queue.lock().unwrap();
            let mut tmp = work_queue.drain(..).collect::<Vec<J>>();
            flushed_work.append(&mut tmp);

            // Note that we don't just call drain() because we want to guarantee atomicity with respect
            // to the work_queue eviction.
            let mut drain_job: J = Default::default();
            drain_job.set_drain(DrainMode::SyntheticDrain);
            work_queue.push_back(drain_job);
        }

        self.awaiting_job_event.write(1).unwrap();

        C2Status::C2Ok
    }

    // Signal to the decoder/encoder that it does not need to wait for
    // additional work to begin processing. This is an unusual name for this
    // function, but it is the convention that C2 uses.
    // State must be C2Running or this function is invalid.
    // State will remain C2Running after the drain is complete.
    //
    // TODO(b/389993558): The "state will remain C2Running after the drain is complete" behavior
    // still needs to be implemented for the encoder.
    //
    // TODO: Support different drain modes.
    pub fn drain(&mut self, _mode: DrainMode) -> C2Status {
        if *self.state.0.lock().expect("Could not lock state") != C2State::C2Running {
            (*self.error_cb.lock().unwrap())(C2Status::C2BadState);
            return C2Status::C2BadState;
        }

        let mut drain_job: J = Default::default();
        drain_job.set_drain(DrainMode::SyntheticDrain);
        self.work_queue.lock().unwrap().push_back(drain_job);

        self.awaiting_job_event.write(1).unwrap();

        C2Status::C2Ok
    }
}

// Instead of C2's release() function, we implement Drop and use RAII to
// accomplish the same thing
impl<J, W> Drop for C2Wrapper<J, W>
where
    J: Send + Default + Job + 'static,
    W: C2Worker<J>,
{
    fn drop(&mut self) {
        // Note: we call reset() instead of stop() so that if we're already
        // C2Stopped, we don't trigger a call to the error callback.
        self.reset();

        let (state_lock, state_cvar) = &*self.state;
        *state_lock.lock().expect("Could not lock state") = C2State::C2Release;
        state_cvar.notify_one();
        self.worker_thread.take().unwrap().join();
    }
}
