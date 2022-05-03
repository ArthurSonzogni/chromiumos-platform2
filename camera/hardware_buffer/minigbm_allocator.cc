// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_buffer/minigbm_allocator.h"

#include <gbm.h>
#include <minigbm/minigbm_helpers.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include <memory>
#include <utility>

#include "cros-camera/common.h"
#include "hardware_buffer/allocator.h"

namespace cros {

namespace {

uint32_t SyncTypeToGbmTransferFlag(SyncType sync_type) {
  switch (sync_type) {
    case SyncType::kSyncRead:
      return GBM_BO_TRANSFER_READ;
    case SyncType::kSyncWrite:
      return GBM_BO_TRANSFER_WRITE;
    case SyncType::kSyncReadWrite:
      return GBM_BO_TRANSFER_READ_WRITE;
  }
}

}  // namespace

MinigbmAllocator::BufferObject::BufferObject(struct gbm_bo* bo,
                                             uint32_t gbm_flags)
    : bo_(bo) {
  CHECK_NE(bo_, nullptr);

  desc_ = {
      .drm_format = gbm_bo_get_format(bo_),
      .width = static_cast<int>(gbm_bo_get_width(bo_)),
      .height = static_cast<int>(gbm_bo_get_height(bo_)),
      .gbm_flags = gbm_flags,
      .num_planes = gbm_bo_get_plane_count(bo_),
      .format_modifier = gbm_bo_get_modifier(bo_),
  };
  for (int i = 0; i < desc_.num_planes; ++i) {
    desc_.planes[i] = {
        .size = static_cast<int>(gbm_bo_get_plane_size(bo_, i)),
        .offset = static_cast<int>(gbm_bo_get_offset(bo_, i)),
        // Should this be per-plane?
        .pixel_stride = static_cast<int>(gbm_bo_get_bpp(bo_)),
        .row_stride = static_cast<int>(gbm_bo_get_stride_for_plane(bo_, i)),
    };
  }
}

MinigbmAllocator::BufferObject::BufferObject(BufferObject&& other) {
  (*this) = std::move(other);
}

MinigbmAllocator::BufferObject& MinigbmAllocator::BufferObject::operator=(
    BufferObject&& other) {
  if (this != &other) {
    Invalidate();
    this->bo_ = other.bo_;
    this->plane_data_ = other.plane_data_;
    this->desc_ = other.desc_;
    other.bo_ = nullptr;
    for (int i = 0; i < kMaxPlanes; ++i) {
      other.plane_data_[i] = {.map_data = nullptr, .addr = nullptr};
    }
    other.desc_ = BufferDescriptor();
  }
  return *this;
}

MinigbmAllocator::BufferObject::~BufferObject() {
  Invalidate();
}

BufferDescriptor MinigbmAllocator::BufferObject::Describe() const {
  return desc_;
}

bool MinigbmAllocator::BufferObject::BeginCpuAccess(SyncType sync_type,
                                                    int plane) {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  if (IsMapped(plane)) {
    Unmap(plane);
  }
  return MapInternal(sync_type, plane);
}

bool MinigbmAllocator::BufferObject::EndCpuAccess(SyncType sync_type,
                                                  int plane) {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  if (IsMapped(plane)) {
    Unmap(plane);
    return MapInternal(sync_type, plane);
  }
  return true;
}

bool MinigbmAllocator::BufferObject::Map(int plane) {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  return MapInternal(SyncType::kSyncReadWrite, plane);
}

void MinigbmAllocator::BufferObject::Unmap(int plane) {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  UnmapInternal(plane);
}

int MinigbmAllocator::BufferObject::GetPlaneFd(int plane) const {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  return gbm_bo_get_plane_fd(bo_, plane);
}

void* MinigbmAllocator::BufferObject::GetPlaneAddr(int plane) const {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  if (!IsMapped(plane)) {
    LOGF(ERROR) << "Buffer 0x" << std::hex << GetId() << " is not mapped";
    return nullptr;
  }
  return plane_data_[plane].addr;
}

uint64_t MinigbmAllocator::BufferObject::GetId() const {
  CHECK_NE(bo_, nullptr);
  return reinterpret_cast<uint64_t>(bo_);
}

bool MinigbmAllocator::BufferObject::MapInternal(SyncType sync_type,
                                                 int plane) {
  if (IsMapped(plane)) {
    return true;
  }
  uint32_t stride = 0;
  void* addr =
      gbm_bo_map2(bo_, 0, 0, gbm_bo_get_width(bo_), gbm_bo_get_height(bo_),
                  SyncTypeToGbmTransferFlag(sync_type), &stride,
                  &plane_data_[plane].map_data, plane);
  if (addr == MAP_FAILED) {
    PLOGF(ERROR) << "Failed to map buffer";
    plane_data_[plane].map_data = nullptr;
    return false;
  }
  plane_data_[plane].addr = addr;
  return true;
}

void MinigbmAllocator::BufferObject::UnmapInternal(int plane) {
  if (!IsMapped(plane)) {
    return;
  }
  if (plane_data_[plane].map_data) {
    gbm_bo_unmap(bo_, plane_data_[plane].map_data);
  }
  plane_data_[plane].map_data = nullptr;
  plane_data_[plane].addr = nullptr;
}

bool MinigbmAllocator::BufferObject::IsMapped(int plane) const {
  CHECK_NE(bo_, nullptr);
  CHECK_LT(plane, desc_.num_planes);
  return plane_data_[plane].map_data != nullptr &&
         plane_data_[plane].addr != nullptr;
}

void MinigbmAllocator::BufferObject::Invalidate() {
  if (bo_) {
    for (size_t i = 0; i < desc_.num_planes; ++i) {
      Unmap(i);
    }
    gbm_bo_destroy(bo_);
    bo_ = nullptr;
  }
}

MinigbmAllocator::MinigbmAllocator(struct gbm_device* gbm_device)
    : gbm_device_(gbm_device) {
  CHECK(gbm_device_);
}

MinigbmAllocator::~MinigbmAllocator() {
  close(gbm_device_get_fd(gbm_device_));
  gbm_device_destroy(gbm_device_);
}

std::unique_ptr<Allocator::BufferObject> MinigbmAllocator::CreateBo(
    int width, int height, uint32_t drm_format, uint32_t gbm_flags) {
  return std::make_unique<BufferObject>(
      gbm_bo_create(gbm_device_, width, height, drm_format, gbm_flags),
      gbm_flags);
}

std::unique_ptr<Allocator::BufferObject> MinigbmAllocator::ImportBo(
    const ImportData& data) {
  auto& desc = data.desc;
  struct gbm_import_fd_modifier_data import_data = {
      .width = static_cast<uint32_t>(desc.width),
      .height = static_cast<uint32_t>(desc.height),
      .format = desc.drm_format,
      .num_fds = static_cast<uint32_t>(desc.num_planes),
      .modifier = desc.format_modifier,
  };
  for (size_t i = 0; i < desc.num_planes; ++i) {
    import_data.fds[i] = data.plane_fd[i];
    import_data.strides[i] = desc.planes[i].row_stride;
    import_data.offsets[i] = desc.planes[i].offset;
  }
  return std::make_unique<BufferObject>(
      gbm_bo_import(gbm_device_, GBM_BO_IMPORT_FD_MODIFIER, &import_data,
                    desc.gbm_flags),
      desc.gbm_flags);
}

bool MinigbmAllocator::IsFormatSupported(uint32_t drm_format,
                                         uint32_t gbm_flags) {
  return gbm_device_is_format_supported(gbm_device_, drm_format, gbm_flags);
}

std::unique_ptr<Allocator> CreateMinigbmAllocator() {
  int unused_fd = -1;
  struct gbm_device* gbm_device = minigbm_create_default_device(&unused_fd);
  if (!gbm_device) {
    LOG(ERROR) << "Minigbm not supported";
    return nullptr;
  }
  return std::make_unique<MinigbmAllocator>(gbm_device);
}

}  // namespace cros
