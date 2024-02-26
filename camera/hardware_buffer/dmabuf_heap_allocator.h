// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HARDWARE_BUFFER_DMABUF_HEAP_ALLOCATOR_H_
#define CAMERA_HARDWARE_BUFFER_DMABUF_HEAP_ALLOCATOR_H_

#include "hardware_buffer/allocator.h"

#include <memory>

namespace cros {

// The DMA-buf Heap Buffer allocator. The buffers are allocated from the DMA-BUF
// Heap drivers.
class DmaBufHeapAllocator : public Allocator {
 public:
  class BufferObject : public Allocator::BufferObject {
   public:
    BufferObject(int fd, size_t size, BufferDescriptor desc);
    BufferObject(const BufferObject& bo) = delete;
    BufferObject& operator=(const BufferObject& bo) = delete;
    BufferObject(BufferObject&& bo);
    BufferObject& operator=(BufferObject&& bo);
    ~BufferObject() override;

    // Allocator::BufferObject implementation.
    BufferDescriptor Describe() const override;
    bool BeginCpuAccess(SyncType sync_type, int plane) override;
    bool EndCpuAccess(SyncType sync_type, int plane) override;
    bool Map(int plane) override;
    void Unmap(int plane) override;
    int GetPlaneFd(int plane) const override;
    void* GetPlaneAddr(int plane) const override;
    uint64_t GetId() const override;

   private:
    bool MapInternal(SyncType sync_type, int plane);
    void UnmapInternal(int plane);
    bool IsMapped(int plane) const;
    void Invalidate();

    // The FD associated with the allocated DMA-buf heap buffer.
    int fd_ = -1;

    // The size of the buffer.
    size_t buffer_size_ = 0;

    // The format layout associated with the buffer.
    BufferDescriptor desc_;

    // The mapped virtual address.
    void* addr_ = nullptr;
  };

  explicit DmaBufHeapAllocator(int dma_heap_device_fd);
  ~DmaBufHeapAllocator() override;

  // Allocator implementation.
  std::unique_ptr<Allocator::BufferObject> CreateBo(
      int width, int height, uint32_t drm_format, uint32_t gbm_flags) override;
  std::unique_ptr<Allocator::BufferObject> ImportBo(
      const ImportData& data) override;
  bool IsFormatSupported(uint32_t drm_format, uint32_t gbm_flags) override;

 private:
  int dma_heap_device_fd_ = -1;
  std::unique_ptr<Allocator> minigbm_allocator_ = nullptr;
};

std::unique_ptr<Allocator> CreateDmaBufHeapAllocator();

}  // namespace cros

#endif  // CAMERA_HARDWARE_BUFFER_DMABUF_HEAP_ALLOCATOR_H_
