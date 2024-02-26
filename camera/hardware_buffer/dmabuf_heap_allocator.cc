// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_buffer/dmabuf_heap_allocator.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "cros-camera/common.h"
#include "hardware_buffer/allocator.h"
#include "hardware_buffer/minigbm_allocator.h"

namespace cros {

namespace {

constexpr char kDmaHeapRoot[] = "/dev/dma_heap/";
constexpr char kDmaBufSystemHeapName[] = "system";

uint64_t SyncTypeToDmaBufSyncFlag(SyncType sync_type) {
  switch (sync_type) {
    case SyncType::kSyncRead:
      return DMA_BUF_SYNC_READ;
    case SyncType::kSyncWrite:
      return DMA_BUF_SYNC_WRITE;
    case SyncType::kSyncReadWrite:
      return DMA_BUF_SYNC_RW;
  }
}

int DmaBufAlloc(const std::string& heap_name, size_t len, int dev_fd) {
  CHECK_GE(dev_fd, 0);

  struct dma_heap_allocation_data heap_data = {
      .len = len,
      .fd_flags = O_RDWR | O_CLOEXEC,
  };
  auto ret =
      TEMP_FAILURE_RETRY(ioctl(dev_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data));
  if (ret < 0) {
    PLOGF(ERROR) << "Unable to allocate from DMA-BUF heap: " << heap_name;
    return ret;
  }
  return heap_data.fd;
}

int DoSync(int dmabuf_fd, bool start, SyncType sync_type) {
  struct dma_buf_sync sync = {
      .flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) |
               SyncTypeToDmaBufSyncFlag(sync_type),
  };
  return TEMP_FAILURE_RETRY(ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync));
}

inline std::string DrmFormatToString(uint32_t drm_format) {
  return std::string(reinterpret_cast<char*>(&drm_format), 4);
}

}  // namespace

DmaBufHeapAllocator::BufferObject::BufferObject(int fd,
                                                size_t buffer_size,
                                                BufferDescriptor desc)
    : fd_(fd), buffer_size_(buffer_size), desc_(std::move(desc)) {
  CHECK_GE(fd_, 0);
  CHECK_GT(buffer_size_, 0);
}

DmaBufHeapAllocator::BufferObject::BufferObject(BufferObject&& other) {
  (*this) = std::move(other);
}

DmaBufHeapAllocator::BufferObject& DmaBufHeapAllocator::BufferObject::operator=(
    BufferObject&& other) {
  if (this != &other) {
    Invalidate();
    this->fd_ = other.fd_;
    this->buffer_size_ = other.buffer_size_;
    this->desc_ = other.desc_;
    this->addr_ = other.addr_;
    other.fd_ = -1;
    other.buffer_size_ = 0;
    other.desc_ = BufferDescriptor();
    other.addr_ = nullptr;
  }
  return *this;
}

DmaBufHeapAllocator::BufferObject::~BufferObject() {
  Invalidate();
}

BufferDescriptor DmaBufHeapAllocator::BufferObject::Describe() const {
  return desc_;
}

bool DmaBufHeapAllocator::BufferObject::BeginCpuAccess(SyncType sync_type,
                                                       int plane) {
  CHECK_LT(plane, desc_.num_planes);
  if (DoSync(fd_, /*start=*/true, sync_type) < 0) {
    PLOGF(ERROR) << "Failed to sync buffer for starting CPU access";
    return false;
  }
  return true;
}

bool DmaBufHeapAllocator::BufferObject::EndCpuAccess(SyncType sync_type,
                                                     int plane) {
  CHECK_LT(plane, desc_.num_planes);
  if (DoSync(fd_, /*start=*/false, sync_type) < 0) {
    PLOGF(ERROR) << "Failed to sync buffer for ending CPU access";
    return false;
  }
  return true;
}

bool DmaBufHeapAllocator::BufferObject::Map(int plane) {
  CHECK_LT(plane, desc_.num_planes);
  if (IsMapped(plane)) {
    return true;
  }
  void* addr =
      mmap(nullptr, buffer_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (addr == MAP_FAILED) {
    LOGF(ERROR) << "Failed to map buffer plane " << plane;
    return false;
  }
  addr_ = addr;
  return true;
}

void DmaBufHeapAllocator::BufferObject::Unmap(int plane) {
  CHECK_LT(plane, desc_.num_planes);
  // Lazy unmap only when the buffer object is destroyed.
}

int DmaBufHeapAllocator::BufferObject::GetPlaneFd(int plane) const {
  CHECK_LT(plane, desc_.num_planes);
  return fd_;
}

void* DmaBufHeapAllocator::BufferObject::GetPlaneAddr(int plane) const {
  CHECK_LT(plane, desc_.num_planes);
  if (!IsMapped(plane)) {
    LOGF(ERROR) << "Buffer 0x" << std::hex << GetId() << " is not mapped";
    return nullptr;
  }
  return reinterpret_cast<uint8_t*>(addr_) + desc_.planes[plane].offset;
}

uint64_t DmaBufHeapAllocator::BufferObject::GetId() const {
  return reinterpret_cast<uint64_t>(this);
}

bool DmaBufHeapAllocator::BufferObject::IsMapped(int plane) const {
  CHECK_LT(plane, desc_.num_planes);
  return addr_ != nullptr;
}

void DmaBufHeapAllocator::BufferObject::Invalidate() {
  if (addr_) {
    munmap(addr_, buffer_size_);
    addr_ = nullptr;
  }
  desc_ = BufferDescriptor();
  buffer_size_ = 0;
  close(fd_);
  fd_ = -1;
}

DmaBufHeapAllocator::DmaBufHeapAllocator(int dma_heap_device_fd)
    : dma_heap_device_fd_(dma_heap_device_fd),
      minigbm_allocator_(CreateMinigbmAllocator()) {
  CHECK_NE(-1, dma_heap_device_fd_);
  if (!minigbm_allocator_) {
    LOGF(WARNING)
        << "Format query will not be supported due to lack of minigbm";
  }
}

DmaBufHeapAllocator::~DmaBufHeapAllocator() {
  close(dma_heap_device_fd_);
}

std::unique_ptr<Allocator::BufferObject> DmaBufHeapAllocator::CreateBo(
    int width, int height, uint32_t drm_format, uint32_t gbm_flags) {
  if (!IsFormatSupported(drm_format, gbm_flags)) {
    LOGF(ERROR) << "Unsupported format " << DrmFormatToString(drm_format)
                << " with flags 0x" << std::hex << gbm_flags;
    return nullptr;
  }

  // TODO(jcliang): Add support for simple aligned-allocation for BLOB buffer
  // without needs to go through minigbm.

  if (!minigbm_allocator_) {
    LOGF(ERROR) << "Minigbm is required to query complex buffer layout";
    return nullptr;
  }
  auto test_bo =
      minigbm_allocator_->CreateBo(width, height, drm_format, gbm_flags);
  if (!test_bo) {
    LOGF(ERROR) << "Test BO allocation failed";
    return nullptr;
  }

  auto desc = test_bo->Describe();

  // Allocate one DMA-heap buffer large enough to hold all the planes and adjust
  // the plane offsets accordingly.
  desc.planes[0].offset = 0;
  size_t buffer_size = desc.planes[0].size;
  for (int i = 1; i < desc.num_planes; ++i) {
    desc.planes[i].offset = desc.planes[i - 1].offset + desc.planes[i - 1].size;
    buffer_size += desc.planes[i].size;
  }

  int buf_fd =
      DmaBufAlloc(kDmaBufSystemHeapName, buffer_size, dma_heap_device_fd_);
  if (buf_fd < 0) {
    return nullptr;
  }

  return std::make_unique<BufferObject>(buf_fd, buffer_size, std::move(desc));
}

std::unique_ptr<Allocator::BufferObject> DmaBufHeapAllocator::ImportBo(
    const ImportData& data) {
  // We don't support importing DMA-buf heap buffers from another process at the
  // moment.
  return nullptr;
}

bool DmaBufHeapAllocator::IsFormatSupported(uint32_t drm_format,
                                            uint32_t gbm_flags) {
  if (!minigbm_allocator_) {
    return false;
  }
  return minigbm_allocator_->IsFormatSupported(drm_format, gbm_flags);
}

std::unique_ptr<Allocator> CreateDmaBufHeapAllocator() {
  std::string heap_path(kDmaHeapRoot);
  heap_path += kDmaBufSystemHeapName;
  int dma_heap_device_fd =
      TEMP_FAILURE_RETRY(open(heap_path.c_str(), O_RDONLY | O_CLOEXEC));
  if (dma_heap_device_fd < 0) {
    PLOGF(ERROR) << "DMA-buf Heap not supported";
    return nullptr;
  }
  return std::make_unique<DmaBufHeapAllocator>(dma_heap_device_fd);
}

}  // namespace cros
