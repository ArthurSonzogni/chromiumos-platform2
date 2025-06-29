// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::_mm_clflush;
#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::_mm_mfence;
use std::cell::RefCell;
use std::fmt;
use std::fmt::Debug;
use std::fs::File;
use std::iter::zip;
use std::mem::replace;
use std::num::NonZeroUsize;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd};
use std::ptr::NonNull;
#[cfg(feature = "vaapi")]
use std::rc::Rc;
use std::slice;
use std::sync::atomic::{fence, Ordering};
#[cfg(feature = "v4l2")]
use std::sync::Arc;

use crate::image_processing::detile_y_tile;
use crate::video_frame::{ReadMapping, VideoFrame, WriteMapping};
#[cfg(feature = "vaapi")]
use crate::DecodedFormat;
use crate::{Fourcc, FrameLayout, Resolution};

use drm_fourcc::DrmModifier;
use nix::errno::Errno;
use nix::ioctl_write_ptr;
use nix::libc;
use nix::poll::poll;
use nix::poll::PollFd;
use nix::poll::PollFlags;
use nix::poll::PollTimeout;
use nix::sys::mman::mmap;
use nix::sys::mman::munmap;
use nix::sys::mman::MapFlags;
use nix::sys::mman::ProtFlags;
use nix::unistd::dup;

use static_assertions::const_assert_eq;

#[cfg(feature = "vaapi")]
use libva::{
    Display, ExternalBufferDescriptor, MemoryType, Surface, UsageHint, VADRMPRIMESurfaceDescriptor,
    VADRMPRIMESurfaceDescriptorLayer, VADRMPRIMESurfaceDescriptorObject,
};
#[cfg(feature = "v4l2")]
use v4l2r::bindings::v4l2_plane;
#[cfg(feature = "v4l2")]
use v4l2r::device::Device;
#[cfg(feature = "v4l2")]
use v4l2r::ioctl::V4l2Buffer;
#[cfg(feature = "v4l2")]
use v4l2r::memory::DmaBufHandle;
#[cfg(feature = "v4l2")]
use v4l2r::Format;

// UNSAFE: This file uses tons of unsafe code because we are directly interacting with the kernel's
// DMA infrastructure. The core assumption is that GenericDmaVideoFrame is initialized with a
// valid DRM Prime File Descriptor, and that the FrameLayout given accurately describes the memory
// layout of the frame. We leverage Rust's lifetime system and RAII design patterns to guarantee
// that mappings will not last longer than the underlying DMA buffer.

// Defined in include/linux/dma-buf.h
const DMA_BUF_BASE: u8 = b'b';
const DMA_BUF_IOCTL_SYNC: u8 = 0;
const DMA_BUF_SYNC_READ: u64 = 1 << 0;
const DMA_BUF_SYNC_WRITE: u64 = 2 << 0;
const DMA_BUF_SYNC_START: u64 = 0 << 2;
const DMA_BUF_SYNC_END: u64 = 1 << 2;
#[repr(C)]
struct dma_buf_sync {
    flags: u64,
}
ioctl_write_ptr!(dma_buf_ioctl_sync, DMA_BUF_BASE, DMA_BUF_IOCTL_SYNC, dma_buf_sync);

fn handle_eintr<T>(cb: &mut impl FnMut() -> nix::Result<T>) -> Result<T, String> {
    loop {
        match cb() {
            Ok(ret) => return Ok(ret),
            Err(errno) => {
                if errno != Errno::EINTR {
                    return Err(format!("Error executing DMA buf sync! {errno}"));
                }
            }
        }
    }
}

/// Offsets an address that was returned by `nix::sys::mman::mmap`.
///
/// # Safety
///
/// The caller asserts that `mmapped_addr` was returned by a successful call to
/// `nix::sys::mman::mmap` for which:
///
/// - The requested `length` was `mmapped_size`.
///
///   AND
///
/// - The requested `offset` was 0.
unsafe fn offset_mmapped_addr(
    mmapped_addr: NonNull<u8>,
    mmapped_size: NonZeroUsize,
    offset_in_bytes: usize,
) -> Result<NonNull<u8>, String> {
    const_assert_eq!(size_of::<u8>(), 1);
    let offset: isize = offset_in_bytes
        .try_into()
        .or(Err("Cannot convert the desired offset to an isize".to_string()))?;

    if offset
        >= mmapped_size.get().try_into().or(Err("Cannot convert the mmapped_size to an isize"))?
    {
        return Err("The desired offset is too large".to_string());
    }
    assert!(offset >= 0);

    // SAFETY: The `offset()` method requires the following in order to be safe [1]:
    //
    // - "The computed offset, count * size_of::<T>() bytes, must not overflow isize":
    //
    //   The assertion that `size_of::<u8>()` is 1 and the conversion of `offset_in_bytes` to an
    //   `isize` guarantee this.
    //
    // - "If the computed offset is non-zero, then self must be derived from a pointer to some
    //   allocated object, and the entire memory range between self and the result must be in bounds
    //   of that allocated object. In particular, this range must not “wrap around” the edge of the
    //   address space":
    //
    //   Since `mmapped_addr` is assumed to come from a successful `mmap()` call with `mmapped_size`
    //   as the `length` and 0 as the `offset`, it's reasonable to assume that all addresses in
    //   [`mmapped_addr`, `mmapped_addr` + `mmapped_size`) are part of an allocated object.
    //   Furthermore, the `offset` check above guarantees that `offset` is in [0, `mmapped_size`) so
    //   the entire memory range between `mmapped_addr` and `mmapped_addr` + `offset` is in that
    //   allocated object.
    //
    //   Additionally, it's reasonable to assume that `mmap()` returns a range that does
    //   not wrap around the edge of the address space.
    //
    // [1] https://doc.rust-lang.org/std/ptr/struct.NonNull.html#method.offset
    Ok(unsafe { mmapped_addr.offset(offset) })
}

pub struct DmaMapping<'a> {
    dma_handles: Vec<BorrowedFd<'a>>,
    mmapped_addrs: Vec<NonNull<u8>>,
    mmap_lens: Vec<usize>,
    plane_addrs: Vec<NonNull<u8>>,
    detiled_bufs: Vec<Vec<u8>>,
    lens: Vec<usize>,
    is_writable: bool,
}

impl<'a> DmaMapping<'a> {
    fn new(
        dma_handles: &'a Vec<File>,
        offsets: Vec<usize>,
        pitches: Vec<usize>,
        lens: Vec<usize>,
        modifier: DrmModifier,
        is_writable: bool,
    ) -> Result<Self, String> {
        if is_writable && modifier != DrmModifier::Linear {
            return Err(
                "Writable mappings currently only supported for linear buffers!".to_string()
            );
        }
        if modifier != DrmModifier::Linear && modifier != DrmModifier::I915_y_tiled {
            return Err(
                "Only linear and Y tile buffers are currently supported for mapping!".to_string()
            );
        }

        let borrowed_dma_handles: Vec<BorrowedFd> = dma_handles.iter().map(|x| x.as_fd()).collect();

        // Wait on all memory fences to finish before attempting to map this DMA buffer.
        for fd in borrowed_dma_handles.iter() {
            let mut fence_poll_fd =
                [PollFd::new(fd.clone(), PollFlags::POLLIN | PollFlags::POLLOUT)];
            poll(&mut fence_poll_fd, PollTimeout::NONE).unwrap();
        }

        // Some architectures do not put DMA in the same coherency zone as CPU, so we need to
        // invalidate cache lines corresponding to this memory. The DMA infrastructure provides
        // this convenient IOCTL for doing so.
        let sync_struct =
            dma_buf_sync { flags: DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE };

        for fd in borrowed_dma_handles.iter() {
            // SAFETY: This assumes fd is a valid DMA buffer.
            handle_eintr(&mut || unsafe { dma_buf_ioctl_sync(fd.as_raw_fd(), &sync_struct) })?;
        }

        let mmap_access = if is_writable {
            ProtFlags::PROT_READ | ProtFlags::PROT_WRITE
        } else {
            ProtFlags::PROT_READ
        };

        // Offsets aren't guaranteed to be page aligned, so we have to map the entire FD and then do
        // pointer arithmetic to get the right buffer. That said, we track the addresses returned by
        // `mmap()` separately from the addresses that result from adding the offset because there
        // may be a single `mmap()` call reused by multiple planes, and we don't want to call
        // `munmap()` later more times than `mmap()` was called.
        let mut mmapped_addrs: Vec<NonNull<u8>> = vec![];
        let mut mmap_lens: Vec<usize> = vec![];
        let mut plane_addrs: Vec<NonNull<u8>> = vec![];
        if borrowed_dma_handles.len() > 1 {
            // In this case, we assume there is one fd per plane.
            if borrowed_dma_handles.len() != offsets.len() {
                return Err(
                    "The number of dma-buf handles doesn't match the number of offsets".to_string()
                );
            }
            if borrowed_dma_handles.len() != lens.len() {
                return Err(
                    "The number of dma-buf handles doesn't match the number of lens".to_string()
                );
            }
            for i in 0..offsets.len() {
                let size_to_map = NonZeroUsize::new(lens[i] + offsets[i])
                    .ok_or("Attempted to map plane of length 0!")?;
                let fd = borrowed_dma_handles[i].as_fd();

                // SAFETY: This assumes that fd is a valid DMA buffer and that our lens and offsets
                // are correct.
                let mmapped_addr = unsafe {
                    mmap(None, size_to_map, mmap_access, MapFlags::MAP_SHARED, fd, 0)
                        .map_err(|err| format!("Error mapping plane {err}"))?
                };
                let mmapped_addr: NonNull<u8> = mmapped_addr.cast();
                mmapped_addrs.push(mmapped_addr);
                mmap_lens.push(size_to_map.into());

                // SAFETY: `mmapped_addr` was returned by a successful call to `mmap()` for which
                // the requested `length` was `size_to_map` and the requested `offset` was 0.
                let plane_addr =
                    unsafe { offset_mmapped_addr(mmapped_addr, size_to_map, offsets[i])? };
                plane_addrs.push(plane_addr);
            }
        } else {
            // In this case, we assume there's one fd that covers all planes.
            let total_size = NonZeroUsize::new(lens.iter().sum::<usize>() + offsets[0])
                .ok_or("Attempted to map VideoFrame of length 0")?;
            let fd = borrowed_dma_handles[0].as_fd();

            // SAFETY: This assumes that fd is a valid DMA buffer and that our lens and offsets are
            // correct.
            let mmapped_addr = unsafe {
                mmap(None, total_size, mmap_access, MapFlags::MAP_SHARED, fd, 0)
                    .map_err(|err| format!("Error mapping plane {err}"))?
            };
            let mmapped_addr: NonNull<u8> = mmapped_addr.cast();
            mmapped_addrs.push(mmapped_addr);
            mmap_lens.push(total_size.into());

            for offset in offsets {
                // SAFETY: `mmapped_addr` was returned by a successful call to `mmap()` for which
                // the requested `length` was `total_size` and the requested `offset` was 0.
                let plane_addr = unsafe { offset_mmapped_addr(mmapped_addr, total_size, offset)? };
                plane_addrs.push(plane_addr);
            }
        }

        let mut detiled_bufs = vec![];
        if modifier == DrmModifier::I915_y_tiled {
            // SAFETY: This assumes mmap returned a valid memory address. Note that nix's mmap
            // bindings already check for null pointers, which we turn into Rust Err objects. So
            // this assumption will only be violated if mmap itself has a bug that returns a
            // non-NULL, but invalid pointer.
            let tiled_bufs: Vec<&[u8]> = unsafe {
                zip(plane_addrs.iter(), lens.iter())
                    .map(|x| slice::from_raw_parts(x.0.as_ptr(), *x.1))
                    .collect()
            };
            // Because we are limited to executing raw mmap instead of leveraging the GEM driver,
            // all of our buffers will be mapped linear even if the backing frame has a modifier.
            // So, we have to manually detile the buffers.
            for i in 0..tiled_bufs.len() {
                let mut detiled_buf: Vec<u8> = vec![];
                detiled_buf.resize(tiled_bufs[i].len(), 0);
                detile_y_tile(
                    detiled_buf.as_mut_slice(),
                    tiled_bufs[i],
                    pitches[i],
                    lens[i] / pitches[i],
                );
                detiled_bufs.push(detiled_buf);
            }
        }

        Ok(DmaMapping {
            dma_handles: borrowed_dma_handles.clone(),
            mmapped_addrs,
            mmap_lens,
            plane_addrs,
            detiled_bufs,
            lens: lens.clone(),
            is_writable,
        })
    }
}

impl<'a> ReadMapping<'a> for DmaMapping<'a> {
    fn get(&self) -> Vec<&[u8]> {
        if self.detiled_bufs.len() > 0 {
            self.detiled_bufs.iter().map(|x| x.as_slice()).collect()
        } else {
            // SAFETY: This assumes mmap returned a valid memory address. Note that nix's mmap
            // bindings already check for null pointers, which we turn into Rust Err objects. So
            // this assumption will only be violated if mmap itself has a bug that returns a
            // non-NULL, but invalid pointer.
            unsafe {
                zip(self.plane_addrs.iter(), self.lens.iter())
                    .map(|x| slice::from_raw_parts(x.0.as_ptr(), *x.1))
                    .collect()
            }
        }
    }
}

impl<'a> WriteMapping<'a> for DmaMapping<'a> {
    fn get(&self) -> Vec<RefCell<&'a mut [u8]>> {
        if !self.is_writable {
            panic!("Attempted to get writable slice to read only mapping!");
        }

        // The above check prevents us from undefined behavior in the event that the user attempts
        // to coerce a ReadMapping into a WriteMapping.
        // SAFETY: This assumes mmap returned a valid memory address. Note that nix's mmap bindings
        // already check for null pointers, which we turn into Rust Err objects. So this assumptoin
        // will only be violated if mmap itself has a bug that returns a non-NULL, but invalid
        // pointer.
        unsafe {
            zip(self.plane_addrs.iter(), self.lens.iter())
                .map(|x| RefCell::new(slice::from_raw_parts_mut(x.0.as_ptr(), *x.1)))
                .collect()
        }
    }
}

impl<'a> Drop for DmaMapping<'a> {
    fn drop(&mut self) {
        // SAFETY: This should be safe because we would not instantiate a DmaMapping object if the
        // first call to dma_buf_ioctl_sync or the mmap call failed.
        unsafe {
            fence(Ordering::SeqCst);

            // Flush all cache lines back to main memory.
            let sync_struct =
                dma_buf_sync { flags: DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE };
            for fd in self.dma_handles.iter() {
                let _ = handle_eintr(&mut || dma_buf_ioctl_sync(fd.as_raw_fd(), &sync_struct));
            }

            // For some reason, DMA_BUF_IOCTL_SYNC is insufficient on Intel machines, and we have
            // to manually flush the cache lines. This is probably a violation of the DMA API spec?
            #[cfg(target_arch = "x86_64")]
            {
                // Note that fence() only guarantees that the compiler won't reorder memory
                // operations, and we need to call _mm_mfence() to guarantee the CPU won't do it.
                _mm_mfence();

                for (addr, len) in zip(self.plane_addrs.iter(), self.lens.iter()) {
                    // TODO: We shouldn't actually have to flush every address, we should just
                    // flush the address at the beginning of each cache line. But, during testing
                    // this caused a race condition.
                    for offset in 0..*len {
                        _mm_clflush((addr.as_ptr()).offset(offset as isize));
                    }
                }

                _mm_mfence();
            }

            fence(Ordering::SeqCst);

            zip(self.mmapped_addrs.iter(), self.mmap_lens.iter())
                .map(|x| munmap((*x.0).cast(), *x.1).unwrap())
                .collect::<Vec<_>>();
        }
    }
}

#[derive(Debug)]
pub struct GenericDmaVideoFrame<T: Clone + Send + Sync + Sized + Debug + 'static> {
    pub token: T,
    dma_handles: Vec<File>,
    layout: FrameLayout,
}

// The Clone trait is implemented for GenericDmaVideoFrame (and importantly no other VideoFrame!)
// just so we can export the frame as a VA-API surface. While this looks risky, in practice we tie
// the lifetimes of the VideoFrames with the Surfaces they are exported to through the VaapiPicture
// struct.
impl<T: Clone + Send + Sync + Sized + Debug + 'static> Clone for GenericDmaVideoFrame<T> {
    fn clone(&self) -> Self {
        Self {
            token: self.token.clone(),
            // SAFETY: This is safe because we are dup'ing the fd, giving the clone'd
            // GenericDmaVideoFrame ownership of the new fd.
            dma_handles: self
                .dma_handles
                .iter()
                .map(|x| unsafe {
                    File::from_raw_fd(dup(x.as_raw_fd()).expect("Could not dup DMAbuf FD!"))
                })
                .collect(),
            layout: self.layout.clone(),
        }
    }
}

impl<T: Clone + Send + Sync + Sized + Debug + 'static> GenericDmaVideoFrame<T> {
    pub fn new(
        token: T,
        dma_handles: Vec<File>,
        layout: FrameLayout,
    ) -> Result<GenericDmaVideoFrame<T>, String> {
        let ret = GenericDmaVideoFrame { token: token, dma_handles: dma_handles, layout: layout };
        ret.validate_frame()?;
        Ok(ret)
    }

    fn get_single_plane_size(&self, index: usize) -> usize {
        if index >= self.num_planes() {
            panic!("Invalid plane index {index}!");
        }

        if self.dma_handles.len() == 1 {
            if index == self.num_planes() - 1 {
                let total_size = self.dma_handles[0].metadata().unwrap().len() as usize;
                total_size - self.layout.planes[index].offset
            } else {
                self.layout.planes[index + 1].offset - self.layout.planes[index].offset
            }
        } else {
            self.dma_handles[index].metadata().unwrap().len() as usize
        }
    }

    fn get_plane_offset(&self) -> Vec<usize> {
        self.layout.planes.iter().map(|x| x.offset).collect()
    }

    fn map_helper(&self, is_writable: bool) -> Result<DmaMapping, String> {
        let lens = self.get_plane_size();
        let pitches = self.get_plane_pitch();
        let offsets = self.get_plane_offset();
        DmaMapping::new(
            &self.dma_handles,
            offsets,
            pitches,
            lens,
            DrmModifier::from(self.layout.format.1),
            is_writable,
        )
    }
}

#[cfg(feature = "vaapi")]
impl<T: Clone + Send + Sync + Sized + Debug + 'static> ExternalBufferDescriptor
    for GenericDmaVideoFrame<T>
{
    const MEMORY_TYPE: MemoryType = MemoryType::DrmPrime2;
    type DescriptorAttribute = VADRMPRIMESurfaceDescriptor;

    fn va_surface_attribute(&mut self) -> Self::DescriptorAttribute {
        let objects = self
            .dma_handles
            .iter()
            .map(|fd| VADRMPRIMESurfaceDescriptorObject {
                fd: fd.as_raw_fd(),
                size: fd.metadata().unwrap().len() as u32,
                drm_format_modifier: self.layout.format.1,
            })
            .chain(std::iter::repeat(Default::default()))
            .take(4)
            .collect::<Vec<_>>();
        let layers = [
            VADRMPRIMESurfaceDescriptorLayer {
                drm_format: u32::from(self.layout.format.0),
                num_planes: self.num_planes() as u32,
                object_index: (0..self.dma_handles.len() as u32)
                    .chain(std::iter::repeat(0))
                    .take(4)
                    .collect::<Vec<_>>()
                    .try_into()
                    .unwrap(),
                offset: self
                    .get_plane_offset()
                    .iter()
                    .map(|x| *x as u32)
                    .chain(std::iter::repeat(0))
                    .take(4)
                    .collect::<Vec<_>>()
                    .try_into()
                    .unwrap(),
                pitch: self
                    .get_plane_pitch()
                    .iter()
                    .map(|x| *x as u32)
                    .chain(std::iter::repeat(0))
                    .take(4)
                    .collect::<Vec<_>>()
                    .try_into()
                    .unwrap(),
            },
            Default::default(),
            Default::default(),
            Default::default(),
        ];
        VADRMPRIMESurfaceDescriptor {
            fourcc: u32::from(self.layout.format.0),
            width: self.layout.size.width,
            height: self.layout.size.height,
            num_objects: self.dma_handles.len() as u32,
            objects: objects.try_into().unwrap(),
            num_layers: 1,
            layers: layers,
        }
    }
}

impl<T: Clone + Send + Sync + Sized + Debug + 'static> VideoFrame for GenericDmaVideoFrame<T> {
    #[cfg(feature = "v4l2")]
    type NativeHandle = DmaBufHandle<File>;

    #[cfg(feature = "vaapi")]
    type MemDescriptor = GenericDmaVideoFrame<T>;
    #[cfg(feature = "vaapi")]
    type NativeHandle = Surface<GenericDmaVideoFrame<T>>;

    fn fourcc(&self) -> Fourcc {
        self.layout.format.0.clone()
    }

    fn modifier(&self) -> u64 {
        self.layout.format.1
    }

    fn resolution(&self) -> Resolution {
        self.layout.size.clone()
    }

    fn get_plane_size(&self) -> Vec<usize> {
        (0..self.num_planes()).map(|idx| self.get_single_plane_size(idx)).collect()
    }

    fn get_plane_pitch(&self) -> Vec<usize> {
        self.layout.planes.iter().map(|x| x.stride).collect()
    }

    fn map<'a>(&'a self) -> Result<Box<dyn ReadMapping<'a> + 'a>, String> {
        Ok(Box::new(self.map_helper(false)?))
    }

    fn map_mut<'a>(&'a mut self) -> Result<Box<dyn WriteMapping<'a> + 'a>, String> {
        Ok(Box::new(self.map_helper(true)?))
    }

    #[cfg(feature = "v4l2")]
    fn fill_v4l2_plane(&self, index: usize, plane: &mut v4l2_plane) {
        if self.dma_handles.len() == 1 {
            plane.m.fd = self.dma_handles[0].as_raw_fd();
            plane.length = self.dma_handles[0].metadata().unwrap().len() as u32;
        } else {
            plane.m.fd = self.dma_handles[index].as_raw_fd();
            plane.length = self.get_single_plane_size(index) as u32;
        }
        // WARNING: Importing DMA buffers with an offset is not officially supported by V4L2, but
        // several drivers (including MTK venc) will respect the data_offset field.
        plane.data_offset = self.layout.planes[index].offset as u32;
    }

    // No-op for DMA buffers since the backing FD already disambiguates them.
    #[cfg(feature = "v4l2")]
    fn process_dqbuf(&mut self, _device: Arc<Device>, _format: &Format, _buf: &V4l2Buffer) {}

    #[cfg(feature = "vaapi")]
    fn to_native_handle(&self, display: &Rc<Display>) -> Result<Self::NativeHandle, String> {
        if self.is_compressed() {
            return Err("Compressed buffer export to VA-API is not currently supported".to_string());
        }
        if !self.is_contiguous() {
            return Err(
                "Exporting non-contiguous GBM buffers to VA-API is not currently supported"
                    .to_string(),
            );
        }

        // TODO: Add more supported formats
        let rt_format = match self.decoded_format().unwrap() {
            DecodedFormat::I420 | DecodedFormat::NV12 => libva::VA_RT_FORMAT_YUV420,
            DecodedFormat::P010 => libva::VA_RT_FORMAT_YUV420_10,
            _ => return Err("Format unsupported for VA-API export".to_string()),
        };

        let mut ret = display
            .create_surfaces(
                rt_format,
                Some(u32::from(self.layout.format.0)),
                self.resolution().width,
                self.resolution().height,
                // TODO: Should we add USAGE_HINT_ENCODER support?
                Some(UsageHint::USAGE_HINT_DECODER),
                vec![self.clone()],
            )
            .map_err(|_| "Error importing GenericDmaVideoFrame to VA-API".to_string())?;

        Ok(ret.pop().unwrap())
    }
}
