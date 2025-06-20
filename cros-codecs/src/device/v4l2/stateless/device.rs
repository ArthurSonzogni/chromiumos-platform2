// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::collections::HashMap;
use std::os::fd::{AsRawFd, RawFd};
use std::rc::{Rc, Weak};
use std::sync::Arc;
use std::thread::sleep;
use std::time::Duration;

use v4l2r::device::Device as VideoDevice;
use v4l2r::device::DeviceConfig;
use v4l2r::ioctl;
use v4l2r::nix::fcntl::open;
use v4l2r::nix::fcntl::OFlag;
use v4l2r::nix::sys::stat::Mode;

use crate::decoder::stateless::DecodeError;
use crate::decoder::stateless::NewStatelessDecoderError;
use crate::device::v4l2::stateless::queue::QueueError;
use crate::device::v4l2::stateless::queue::V4l2CaptureBuffer;
use crate::device::v4l2::stateless::queue::V4l2CaptureQueue;
use crate::device::v4l2::stateless::queue::V4l2OutputQueue;
use crate::device::v4l2::stateless::request::V4l2Request;
use crate::device::v4l2::utils::enumerate_devices;
use crate::video_frame::VideoFrame;
use crate::Fourcc;
use crate::Resolution;

//TODO: handle other memory backends for OUTPUT queue
//TODO: handle video formats other than h264
//TODO: handle queue start/stop at runtime
//TODO: handle DRC at runtime
struct DeviceHandle<V: VideoFrame> {
    video_device: Arc<VideoDevice>,
    media_device: RawFd,
    output_queue: V4l2OutputQueue,
    capture_queue: V4l2CaptureQueue<V>,
    requests: HashMap<u64, Weak<RefCell<V4l2Request<V>>>>,
}

impl<V: VideoFrame> DeviceHandle<V> {
    fn new(format: Fourcc) -> Result<Self, NewStatelessDecoderError> {
        let devices = enumerate_devices(format);
        let (video_device_path, media_device_path) = match devices {
            Some(paths) => paths,
            None => return Err(NewStatelessDecoderError::DriverInitialization),
        };

        let video_device_config = DeviceConfig::new().non_blocking_dqbuf();
        let video_device = Arc::new(
            VideoDevice::open(&video_device_path, video_device_config)
                .map_err(|_| NewStatelessDecoderError::DriverInitialization)?,
        );

        let media_device =
            open(&media_device_path, OFlag::O_RDWR | OFlag::O_CLOEXEC, Mode::empty())
                .map_err(|_| NewStatelessDecoderError::DriverInitialization)?;
        let output_queue = V4l2OutputQueue::new(video_device.clone())?;
        let capture_queue = V4l2CaptureQueue::new(video_device.clone())?;
        Ok(Self {
            video_device,
            media_device,
            output_queue,
            capture_queue,
            requests: HashMap::<u64, Weak<RefCell<V4l2Request<V>>>>::new(),
        })
    }

    pub fn reset_queues(&mut self) -> Result<(), QueueError> {
        self.output_queue
            .reset(self.video_device.clone())
            .map_err(|_| QueueError::ResetOutputQueue)?;
        self.capture_queue
            .reset(self.video_device.clone())
            .map_err(|_| QueueError::ResetCaptureQueue)
    }

    fn alloc_request(&self) -> ioctl::Request {
        ioctl::Request::alloc(&self.media_device).expect("Failed to alloc request handle")
    }
    fn dequeue_output_buffer(&self) {
        let mut back_off_duration = Duration::from_millis(1);
        loop {
            match self.output_queue.dequeue_buffer() {
                Ok(_) => {
                    break;
                }
                Err(QueueError::BufferDequeue) => {
                    sleep(back_off_duration);
                    back_off_duration = back_off_duration + back_off_duration;
                    continue;
                }
                Err(_) => panic!("handle this better"),
            }
        }
    }
    fn insert_request_into_hash(&mut self, request: Weak<RefCell<V4l2Request<V>>>) {
        let timestamp = request.upgrade().unwrap().as_ref().borrow().timestamp();
        self.requests.insert(timestamp, request);
    }
    fn try_dequeue_capture_buffers(&mut self) {
        loop {
            match self.capture_queue.dequeue_buffer() {
                Ok(Some(buffer)) => {
                    let timestamp = buffer.timestamp();
                    let request = self.requests.remove(&timestamp).unwrap();
                    match request.upgrade().unwrap().as_ref().try_borrow_mut() {
                        Ok(mut request) => {
                            request.associate_dequeued_buffer(buffer);
                        }
                        _ => (),
                    }
                    continue;
                }
                _ => break,
            }
        }
    }
    fn sync(&mut self, timestamp: u64) -> V4l2CaptureBuffer<V> {
        let mut back_off_duration = Duration::from_millis(1);
        let time_out = Duration::from_millis(120);
        loop {
            match self.capture_queue.dequeue_buffer() {
                Ok(Some(buffer)) => {
                    let dequeued_timestamp = buffer.timestamp();
                    let request = self.requests.remove(&dequeued_timestamp).unwrap();
                    if dequeued_timestamp == timestamp {
                        return buffer;
                    } else {
                        match request.upgrade().unwrap().as_ref().try_borrow_mut() {
                            Ok(mut request) => {
                                request.associate_dequeued_buffer(buffer);
                            }
                            _ => (),
                        }
                    }
                }
                _ => (),
            }
            back_off_duration = back_off_duration + back_off_duration;
            if back_off_duration > time_out {
                panic!("there should not be a scenario where a queued frame is not returned.");
            }
            sleep(back_off_duration);
        }
    }
}

pub struct V4l2Device<V: VideoFrame> {
    handle: Option<Rc<RefCell<DeviceHandle<V>>>>,
}

impl<V: VideoFrame> V4l2Device<V> {
    pub fn new() -> Result<Self, NewStatelessDecoderError> {
        Ok(Self { handle: None })
    }

    pub fn reset_queues(&mut self) -> Result<(), QueueError> {
        if self.handle.is_none() {
            log::debug!("attempting to reset queues before device is initialized.");
            return Ok(());
        }
        self.handle.as_ref().ok_or(QueueError::InvalidDevice)?.borrow_mut().reset_queues()
    }

    pub fn get_video_device(&mut self) -> Arc<VideoDevice> {
        return self.handle.as_ref().unwrap().borrow_mut().video_device.clone();
    }

    pub fn initialize_output_queue(
        &mut self,
        format: Fourcc,
        coded_size: Resolution,
    ) -> Result<(), anyhow::Error> {
        if self.handle.is_none() {
            self.handle = Some(Rc::new(RefCell::new(DeviceHandle::new(format)?)));
        }

        let mut handle = self.handle.as_ref().ok_or(QueueError::InvalidDevice)?.borrow_mut();
        handle.output_queue.initialize(format, coded_size)?;
        Ok(())
    }

    pub fn initialize_capture_queue(&mut self, num_buffers: u32) -> Result<(), anyhow::Error> {
        let mut handle = self.handle.as_ref().ok_or(QueueError::InvalidDevice)?.borrow_mut();
        handle.capture_queue.initialize(num_buffers)?;
        Ok(())
    }

    pub fn alloc_request(
        &self,
        timestamp: u64,
        frame: V,
    ) -> Result<Rc<RefCell<V4l2Request<V>>>, DecodeError> {
        let mut handle = self.handle.as_ref().ok_or(QueueError::InvalidDevice)?.borrow_mut();

        if handle.capture_queue.num_free_buffers() == 0 {
            return Err(DecodeError::NotEnoughOutputBuffers(0));
        }

        let output_buffer = handle.output_queue.alloc_buffer();

        let output_buffer = match output_buffer {
            Ok(buffer) => buffer,
            Err(DecodeError::NotEnoughOutputBuffers(_)) => {
                handle.dequeue_output_buffer();
                match handle.output_queue.alloc_buffer() {
                    Ok(buffer) => buffer,
                    Err(e) => return Err(e),
                }
            }
            Err(error) => return Err(error),
        };
        handle.try_dequeue_capture_buffers();

        let request = Rc::new(RefCell::new(V4l2Request::new(
            self.clone(),
            timestamp,
            handle.alloc_request(),
            output_buffer,
            frame,
        )));
        handle.insert_request_into_hash(Rc::downgrade(&request.clone()));
        Ok(request)
    }
    pub fn sync(&self, timestamp: u64) -> Result<V4l2CaptureBuffer<V>, QueueError> {
        Ok(self.handle.as_ref().ok_or(QueueError::InvalidDevice)?.borrow_mut().sync(timestamp))
    }
    pub fn queue_capture_buffer(&self, frame: V) -> Result<(), QueueError> {
        self.handle
            .as_ref()
            .ok_or(QueueError::InvalidDevice)?
            .borrow()
            .capture_queue
            .queue_buffer(frame)
    }
}

impl<V: VideoFrame> Clone for V4l2Device<V> {
    fn clone(&self) -> Self {
        Self { handle: self.handle.clone() }
    }
}

impl<V: VideoFrame> AsRawFd for V4l2Device<V> {
    fn as_raw_fd(&self) -> i32 {
        self.handle.as_ref().unwrap().borrow().video_device.as_raw_fd()
    }
}
