// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HARDWARE_BUFFER_MINIGBM_ALLOCATOR_H_
#define CAMERA_HARDWARE_BUFFER_MINIGBM_ALLOCATOR_H_

#include "hardware_buffer/allocator.h"

#include <gbm.h>

#include <array>
#include <memory>

namespace cros {

// The Minigbm Buffer allocator. The buffers are allocated from the graphics
// drivers through libminigbm.
class MinigbmAllocator : public Allocator {
 public:
  class BufferObject : public Allocator::BufferObject {
   public:
    BufferObject(struct gbm_bo* bo, uint32_t gbm_flags);
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

    static constexpr int kMaxPlanes = 4;

    struct PlaneData {
      // The per-plane map data returned by gbm_bo_map().
      void* map_data = nullptr;
      // The mapped virtual address.
      void* addr = nullptr;
    };

    // The gbm_bo associated with the buffer.
    struct gbm_bo* bo_ = nullptr;
    std::array<PlaneData, kMaxPlanes> plane_data_;

    BufferDescriptor desc_;
  };

  explicit MinigbmAllocator(struct gbm_device* gbm_device);
  ~MinigbmAllocator() override;

  // Allocator implementation.
  std::unique_ptr<Allocator::BufferObject> CreateBo(
      int width, int height, uint32_t drm_format, uint32_t gbm_flags) override;
  std::unique_ptr<Allocator::BufferObject> ImportBo(
      const ImportData& data) override;
  bool IsFormatSupported(uint32_t drm_format, uint32_t gbm_flags) override;

 private:
  struct gbm_device* gbm_device_ = nullptr;
};

std::unique_ptr<Allocator> CreateMinigbmAllocator();

}  // namespace cros

#endif  // CAMERA_HARDWARE_BUFFER_MINIGBM_ALLOCATOR_H_
