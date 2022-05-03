/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_
#define CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_

#include "cros-camera/camera_buffer_manager.h"

#include <map>
#include <memory>

#include <base/synchronization/lock.h>

#include "hardware_buffer/allocator.h"

// A V4L2 extension format which represents 32bit RGBX-8-8-8-8 format. This
// corresponds to DRM_FORMAT_XBGR8888 which is used as the underlying format for
// the HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINEND format on all CrOS boards.
#define V4L2_PIX_FMT_RGBX32 v4l2_fourcc('X', 'B', '2', '4')
#define V4L2_PIX_FMT_P010 v4l2_fourcc('P', '0', '1', '0')
#define V4L2_PIX_FMT_P010M v4l2_fourcc('P', 'M', '1', '0')

// A 10-bit bayer format for private reprocessing on MediaTek ISP P1. It's a
// private RAW format that other DRM drivers will never support and thus making
// it not upstreamable (i.e., defined in official DRM headers). The define
// should be kept in sync with cs/chromeos_public/src/platform/minigbm/drv.h
#define DRM_FORMAT_MTISP_SXYZW10 fourcc_code('M', 'B', '1', '0')

struct native_handle;
typedef const native_handle* buffer_handle_t;
struct android_ycbcr;

namespace cros {

class CameraBufferManagerImpl : public CameraBufferManager {
 public:
  explicit CameraBufferManagerImpl(std::unique_ptr<Allocator> allocator);
  CameraBufferManagerImpl(const CameraBufferManagerImpl&) = delete;
  CameraBufferManagerImpl& operator=(const CameraBufferManagerImpl&) = delete;

  // CameraBufferManager implementation.
  ~CameraBufferManagerImpl() override = default;
  int Allocate(size_t width,
               size_t height,
               uint32_t format,
               uint32_t hal_usage,
               buffer_handle_t* out_buffer,
               uint32_t* out_stride) override;
  int Free(buffer_handle_t buffer) override;
  int Register(buffer_handle_t buffer) override;
  int Deregister(buffer_handle_t buffer) override;
  int Lock(buffer_handle_t buffer,
           uint32_t hal_usage,
           uint32_t x,
           uint32_t y,
           uint32_t width,
           uint32_t height,
           void** out_addr) override;
  int LockYCbCr(buffer_handle_t buffer,
                uint32_t hal_usage,
                uint32_t x,
                uint32_t y,
                uint32_t width,
                uint32_t height,
                struct android_ycbcr* out_ycbcr) override;
  int Unlock(buffer_handle_t buffer) override;
  uint32_t ResolveDrmFormat(uint32_t hal_format, uint32_t hal_usage) override;

 private:
  friend class CameraBufferManager;

  struct BufferContext {
    // The backing buffer object of the DMA-buf.
    std::unique_ptr<Allocator::BufferObject> bo;

    int refcount = 0;
  };

  // Resolves the HAL pixel format |hal_format| to the actual DRM format, based
  // on the gralloc usage flags set in |hal_usage|. The |gbm_flags| will be set
  // if the format is resolved successfully.
  uint32_t ResolveFormat(uint32_t hal_format,
                         uint32_t hal_usage,
                         uint32_t* gbm_flags);

  // Maps |buffer| and returns the mapped address.
  //
  // Args:
  //    |buffer|: The buffer handle to map.
  //    |hal_usage|:  Currently omitted and is reserved for future use.
  //    |plane|: The plane to map.
  //
  // Returns:
  //    The mapped address on success; MAP_FAILED on failure.
  void* Map(buffer_handle_t buffer, uint32_t hal_usage, uint32_t plane);

  // Unmaps |buffer|.
  //
  // Args:
  //    |buffer|: The buffer handle to unmap.
  //    |plane|: The plane to unmap.
  //
  // Returns:
  //    0 on success; -EINVAL if |buffer| is invalid.
  int Unmap(buffer_handle_t buffer, uint32_t plane);

  // Lock to guard access member variables.
  base::Lock lock_;

  std::unique_ptr<Allocator> allocator_;
  std::map<buffer_handle_t, BufferContext> buffers_ GUARDED_BY(lock_);
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_
