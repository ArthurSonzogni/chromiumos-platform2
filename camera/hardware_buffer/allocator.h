// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HARDWARE_BUFFER_ALLOCATOR_H_
#define CAMERA_HARDWARE_BUFFER_ALLOCATOR_H_

#include <drm_fourcc.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#ifndef CROS_CAMERA_EXPORT
#define CROS_CAMERA_EXPORT __attribute__((visibility("default")))
#endif

namespace cros {

constexpr int kMaxPlanes = 4;

struct PlaneDescriptor {
  int size = 0;
  int offset = 0;
  int pixel_stride = 0;
  int row_stride = 0;
};

struct BufferDescriptor {
  uint32_t drm_format = DRM_FORMAT_INVALID;
  int width = 0;
  int height = 0;
  uint32_t gbm_flags = 0;
  int num_planes = 0;
  uint64_t format_modifier = DRM_FORMAT_MOD_INVALID;
  PlaneDescriptor planes[kMaxPlanes];
};

struct ImportData {
  BufferDescriptor desc;
  int plane_fd[kMaxPlanes];
};

// Buffer sync type for read, write or read/write.
enum class SyncType {
  kSyncRead,
  kSyncWrite,
  kSyncReadWrite,
};

// The buffer allocator interface.
class CROS_CAMERA_EXPORT Allocator {
 public:
  // The actual backend handling the buffer allocation and synchronization.
  enum class Backend {
    // Minigbm backed by graphics DRM drivers.
    kMinigbm,

    // DMA-buf heap exposed by the DMA-BUF heaps drivers.
    kDmaBufHeap,
  };

  static std::unique_ptr<Allocator> Create(Backend backend);

  // BufferObject interface used to manage and access the backing storage
  // allocated for a buffer.
  class BufferObject {
   public:
    virtual ~BufferObject() = default;
    virtual BufferDescriptor Describe() const = 0;
    virtual bool BeginCpuAccess(SyncType sync_type, int plane) = 0;
    virtual bool EndCpuAccess(SyncType sync_type, int plane) = 0;
    virtual bool Map(int plane) = 0;
    virtual void Unmap(int plane) = 0;
    virtual int GetPlaneFd(int plane) const = 0;
    virtual void* GetPlaneAddr(int plane) const = 0;
    virtual uint64_t GetId() const = 0;
  };

  virtual ~Allocator() = default;
  virtual std::unique_ptr<BufferObject> CreateBo(int width,
                                                 int height,
                                                 uint32_t drm_format,
                                                 uint32_t gbm_flags) = 0;
  virtual std::unique_ptr<BufferObject> ImportBo(const ImportData& data) = 0;
  virtual bool IsFormatSupported(uint32_t drm_format, uint32_t gbm_flags) = 0;
};

}  // namespace cros

#endif  // CAMERA_HARDWARE_BUFFER_ALLOCATOR_H_
