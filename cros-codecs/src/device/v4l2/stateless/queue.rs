// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;
use thiserror::Error;

use v4l2r::bindings::v4l2_format;
use v4l2r::device::queue::direction;
use v4l2r::device::queue::direction::Capture;
use v4l2r::device::queue::direction::Output;
use v4l2r::device::queue::dqbuf::DqBuffer;
use v4l2r::device::queue::qbuf::QBuffer;
use v4l2r::device::queue::BuffersAllocated;
use v4l2r::device::queue::CreateQueueError;
use v4l2r::device::queue::GetFreeCaptureBuffer;
use v4l2r::device::queue::GetFreeOutputBuffer;
use v4l2r::device::queue::Queue;
use v4l2r::device::queue::QueueInit;
use v4l2r::device::queue::RequestBuffersError;
use v4l2r::device::AllocatedQueue;
use v4l2r::device::Device;
use v4l2r::device::Stream;
use v4l2r::device::TryDequeue;
use v4l2r::ioctl::GFmtError;
use v4l2r::ioctl::SFmtError;
use v4l2r::ioctl::StreamOnError;
use v4l2r::memory::MemoryType;
use v4l2r::memory::MmapHandle;
use v4l2r::nix::sys::time::TimeVal;
use v4l2r::Format;
use v4l2r::PlaneLayout;

use crate::decoder::stateless::DecodeError;
use crate::decoder::stateless::StatelessBackendError;
use crate::image_processing::mm21_to_nv12;
use crate::image_processing::nv12_to_i420;
use crate::image_processing::MM21_TILE_HEIGHT;
use crate::DecodedFormat;
use crate::Fourcc;
use crate::Rect;
use crate::Resolution;

//TODO: handle memory backends other than mmap
//TODO: handle video formats other than h264
//TODO: handle queue start/stop at runtime
//TODO: handle DRC at runtime
//TODO: handle synced buffers in Streaming state
#[derive(Default)]
enum V4l2QueueHandle<T: v4l2r::device::queue::direction::Direction> {
    Init(Queue<T, QueueInit>),
    Streaming(Rc<Queue<T, BuffersAllocated<Vec<MmapHandle>>>>),
    #[default]
    Unknown,
}

#[derive(Debug, Error)]
pub enum QueueError {
    #[error("unable to create queue.")]
    Creation,
    #[error("failed to get format for queue.")]
    FormatGet,
    #[error("failed to set format for queue.")]
    FormatSet,
    #[error("failed requesting buffers.")]
    RequestBuffers,
    #[error("unable to stream on.")]
    StreamOn,
    #[error("driver does not support {0}.")]
    UnsupportedPixelFormat(Fourcc),
    #[error("operation can not be performed in this state.")]
    State,
    #[error("no buffer to dequeue.")]
    Dequeue,
}

impl From<QueueError> for DecodeError {
    fn from(err: QueueError) -> Self {
        DecodeError::BackendError(StatelessBackendError::Other(anyhow::anyhow!(err)))
    }
}

impl From<CreateQueueError> for QueueError {
    fn from(_err: CreateQueueError) -> Self {
        QueueError::Creation
    }
}

impl From<GFmtError> for QueueError {
    fn from(_err: GFmtError) -> Self {
        QueueError::FormatGet
    }
}

impl From<SFmtError> for QueueError {
    fn from(_err: SFmtError) -> Self {
        QueueError::FormatSet
    }
}

impl From<RequestBuffersError> for QueueError {
    fn from(_err: RequestBuffersError) -> Self {
        QueueError::RequestBuffers
    }
}

impl From<StreamOnError> for QueueError {
    fn from(_err: StreamOnError) -> Self {
        QueueError::StreamOn
    }
}

//TODO: handle memory backends other than mmap
pub struct V4l2OutputBuffer {
    queue: V4l2OutputQueue,
    handle: QBuffer<
        Output,
        Vec<MmapHandle>,
        Vec<MmapHandle>,
        Rc<Queue<Output, BuffersAllocated<Vec<MmapHandle>>>>,
    >,
    length: usize,
}

impl V4l2OutputBuffer {
    fn new(
        queue: V4l2OutputQueue,
        handle: QBuffer<
            Output,
            Vec<MmapHandle>,
            Vec<MmapHandle>,
            Rc<Queue<Output, BuffersAllocated<Vec<MmapHandle>>>>,
        >,
    ) -> Self {
        Self {
            queue,
            handle,
            length: 0,
        }
    }
    pub fn index(&self) -> usize {
        self.handle.index()
    }
    pub fn length(&self) -> usize {
        self.length
    }
    pub fn write(&mut self, data: &[u8]) -> &mut Self {
        let mut mapping = self
            .handle
            .get_plane_mapping(0)
            .expect("Failed to mmap output buffer");

        mapping.as_mut()[self.length..self.length + data.len()].copy_from_slice(data);
        self.length += data.len();

        drop(mapping);
        self
    }
    pub fn submit(self, timestamp: u64, request_fd: i32) -> Result<(), QueueError> {
        let handle = &*self.queue.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(_) => {
                self.handle
                    .set_timestamp(TimeVal::new(/* FIXME: sec */ 0, timestamp as i64))
                    .set_request(request_fd)
                    .queue(&[self.length])
                    .expect("Failed to queue output buffer");
                Ok(())
            }
            _ => Err(QueueError::State),
        }
    }
}

#[derive(Clone)]
pub struct V4l2OutputQueue {
    handle: Rc<RefCell<V4l2QueueHandle<direction::Output>>>,
}

fn buffer_size_for_area(width: u32, height: u32) -> u32 {
    let area = width * height;
    let mut buffer_size: u32 = 1024 * 1024;

    if area > 720 * 480 {
        buffer_size *= 2;
    }
    if area > 1920 * 1080 {
        buffer_size *= 2;
    }
    buffer_size
}

const NUM_OUTPUT_BUFFERS: u32 = 2;
impl V4l2OutputQueue {
    pub fn new(device: Arc<Device>) -> Self {
        let handle = Queue::get_output_mplane_queue(device).expect("Failed to get output queue");
        log::debug!("Output queue:\n\tstate: None -> Init\n");
        let handle = Rc::new(RefCell::new(V4l2QueueHandle::Init(handle)));
        Self { handle }
    }

    pub fn initialize(
        &mut self,
        fourcc: Fourcc,
        resolution: Resolution,
    ) -> Result<&mut Self, QueueError> {
        self.handle.replace(match self.handle.take() {
            V4l2QueueHandle::Init(mut handle) => {
                let (width, height) = resolution.into();
                handle
                    .change_format()?
                    .set_size(width as usize, height as usize)
                    .set_pixelformat(fourcc)
                    .set_planes_layout(vec![PlaneLayout {
                        sizeimage: buffer_size_for_area(width, height),
                        ..Default::default()
                    }])
                    .apply::<v4l2_format>()?;

                let format: Format = handle.get_format()?;
                if format.pixelformat != fourcc.into() {
                    return Err(QueueError::UnsupportedPixelFormat(fourcc));
                }

                let handle = handle.request_buffers_generic::<Vec<MmapHandle>>(
                    MemoryType::Mmap,
                    NUM_OUTPUT_BUFFERS,
                )?;

                handle.stream_on()?;

                V4l2QueueHandle::Streaming(handle.into())
            }
            _ => {
                todo!("DRC is not supported")
            }
        });
        Ok(self)
    }

    pub fn num_free_buffers(&self) -> usize {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => handle.num_free_buffers(),
            _ => 0,
        }
    }
    pub fn alloc_buffer(&self) -> Result<V4l2OutputBuffer, DecodeError> {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => match handle.try_get_free_buffer() {
                Ok(buffer) => Ok(V4l2OutputBuffer::new(self.clone(), buffer)),
                Err(_) => Err(DecodeError::NotEnoughOutputBuffers(1)),
            },
            _ => Err(DecodeError::DecoderError(anyhow!(
                "Invalid hardware handle"
            ))),
        }
    }
    pub fn drain(&self) -> Result<(), QueueError> {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => loop {
                if let Err(_) = handle.try_dequeue() {
                    break Ok(());
                }
            },
            _ => return Err(QueueError::State),
        }
    }
    pub fn dequeue_buffer(&self) -> Result<(), QueueError> {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => {
                handle.try_dequeue().map_err(|_| QueueError::Dequeue)?;
                Ok(())
            }
            _ => Err(QueueError::State),
        }
    }
}

// TODO: handle other memory backends
pub struct V4l2CaptureBuffer {
    handle: DqBuffer<Capture, Vec<MmapHandle>>,
    visible_rect: Rect,
    format: Format,
}

impl V4l2CaptureBuffer {
    fn new(handle: DqBuffer<Capture, Vec<MmapHandle>>, visible_rect: Rect, format: Format) -> Self {
        Self {
            handle,
            visible_rect,
            format,
        }
    }
    pub fn index(&self) -> usize {
        self.handle.data.index() as usize
    }
    pub fn timestamp(&self) -> u64 {
        self.handle.data.timestamp().tv_usec as u64
    }
    // TODO enable once upstream v4l2r has rolled
    //    pub fn has_error(&self) -> bool {
    //        self.handle.data.has_error() as u64
    //    }

    //TODO make this work for formats other then 420
    pub fn length(&self) -> usize {
        (Resolution::from(self.visible_rect).get_area() * 3) / 2
    }
    pub fn read(&self, data: &mut [u8]) {
        let decoded_format: DecodedFormat = self
            .format
            .pixelformat
            .to_string()
            .parse()
            .expect("Unable to output");

        match decoded_format {
            DecodedFormat::NV12 => {
                let plane = self
                    .handle
                    .get_plane_mapping(0)
                    .expect("Failed to mmap capture buffer");
                let src_y_stride = self.format.plane_fmt[0].bytesperline as usize;
                let src_uv_stride = self.format.plane_fmt[1].bytesperline as usize;
                let width = Resolution::from(self.visible_rect).width as usize;
                let height = Resolution::from(self.visible_rect).height as usize;
                let (src_y, src_uv) = plane.split_at(src_y_stride * height);
                let (data_y, data_uv) =
                    data.split_at_mut(Resolution::from(self.visible_rect).get_area());
                let (data_u, data_v) =
                    data_uv.split_at_mut((((width + 1) / 2) * ((height + 1) / 2)) as usize);

                nv12_to_i420(
                    &src_y,
                    src_y_stride,
                    data_y,
                    width,
                    &src_uv,
                    src_uv_stride,
                    data_u,
                    (width + 1) / 2,
                    data_v,
                    (width + 1) / 2,
                    width,
                    height,
                );
            }
            DecodedFormat::MM21 => {
                // check planes count
                self.handle.data.num_planes();
                let src_y = self
                    .handle
                    .get_plane_mapping(0)
                    .expect("Failed to mmap capture buffer");
                let src_uv = self
                    .handle
                    .get_plane_mapping(1)
                    .expect("Failed to mmap capture buffer");
                let width = Resolution::from(self.visible_rect).width as usize;
                let height = Resolution::from(self.visible_rect).height as usize;
                // TODO: Replace with align_up function.
                let height_tiled = height + (MM21_TILE_HEIGHT - 1) & !(MM21_TILE_HEIGHT - 1);
                let y_stride = self.format.plane_fmt[0].bytesperline as usize;
                let uv_stride = self.format.plane_fmt[1].bytesperline as usize;
                let mut pivot_y = vec![0; y_stride * height_tiled];
                let mut pivot_uv = vec![0; y_stride * height_tiled / 2];
                mm21_to_nv12(
                    &src_y,
                    &mut pivot_y,
                    &src_uv,
                    &mut pivot_uv,
                    y_stride,
                    height_tiled,
                )
                .expect("Unable to convert mm21 to nv12");

                let (data_y, data_uv) =
                    data.split_at_mut(Resolution::from(self.visible_rect).get_area());
                // TODO: Replace with align_up function.
                let (data_u, data_v) = data_uv.split_at_mut(
                    (((self.visible_rect.width + 1) / 2) * ((self.visible_rect.height + 1) / 2))
                        as usize,
                );
                nv12_to_i420(
                    &pivot_y,
                    y_stride,
                    data_y,
                    width,
                    &pivot_uv,
                    uv_stride,
                    data_u,
                    (width + 1) / 2,
                    data_v,
                    (width + 1) / 2,
                    width,
                    height,
                );
            }
            _ => panic!("handle me"),
        }
    }
}

pub struct V4l2CaptureQueue {
    handle: RefCell<V4l2QueueHandle<direction::Capture>>,
    num_buffers: u32,
    visible_rect: Rect,
    format: Format,
}

impl V4l2CaptureQueue {
    pub fn new(device: Arc<Device>) -> Self {
        let handle = Queue::get_capture_mplane_queue(device).expect("Failed to get capture queue");
        log::debug!("Capture queue:\n\tstate: None -> Init\n");
        let handle = RefCell::new(V4l2QueueHandle::Init(handle));
        Self {
            handle,
            num_buffers: 0,
            visible_rect: Default::default(),
            format: Default::default(),
        }
    }

    pub fn initialize(
        &mut self,
        visible_rect: Rect,
        num_buffers: u32,
    ) -> Result<&mut Self, QueueError> {
        self.visible_rect = visible_rect;
        // TODO: 20 is chosen as a magic number necessary to keep the buffers
        // flowing. Ideally it would be as close to the dpb as possible.
        self.num_buffers = num_buffers + 20;
        self.handle.replace(match self.handle.take() {
            V4l2QueueHandle::Init(handle) => {
                // TODO: check if decoded format is supported.
                self.format = handle.get_format()?;
                // TODO: handle 10 bit format negotiation.
                let handle = handle.request_buffers_generic::<Vec<MmapHandle>>(
                    MemoryType::Mmap,
                    self.num_buffers,
                )?;

                handle.stream_on()?;

                V4l2QueueHandle::Streaming(handle.into())
            }
            _ => todo!("DRC is not supported"),
        });

        Ok(self)
    }

    pub fn dequeue_buffer(&self) -> Result<Option<V4l2CaptureBuffer>, QueueError> {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => match handle.try_dequeue() {
                Ok(buffer) => {
                    // TODO handle buffer dequeuing successfully, but having an error
                    // buffer.data.has_error();
                    Ok(Some(V4l2CaptureBuffer::new(
                        buffer,
                        self.visible_rect,
                        self.format.clone(),
                    )))
                }
                _ => Ok(None),
            },
            _ => Err(QueueError::State),
        }
    }
    pub fn queue_buffer(&self) -> Result<(), QueueError> {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => {
                let buffer = handle
                    .try_get_free_buffer()
                    .expect("Failed to alloc capture buffer");
                log::debug!("capture >> index: {}\n", buffer.index());
                buffer.queue().expect("Failed to queue capture buffer");
            }
            _ => return Err(QueueError::State),
        }
        Ok(())
    }
    pub fn num_buffers(&self) -> usize {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => handle.num_buffers(),
            _ => 0,
        }
    }
    pub fn num_free_buffers(&self) -> usize {
        let handle = &*self.handle.borrow();
        match handle {
            V4l2QueueHandle::Streaming(handle) => handle.num_free_buffers(),
            _ => 0,
        }
    }
}
