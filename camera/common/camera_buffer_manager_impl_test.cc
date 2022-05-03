/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_buffer_manager_impl.h"

#include <drm_fourcc.h>
#include <gbm.h>
#include <sys/mman.h>

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/camera_buffer_handle.h"
#include "hardware_buffer/allocator.h"

namespace cros::tests {

using ::testing::A;
using ::testing::ByMove;
using ::testing::Return;

void* fake_addr = reinterpret_cast<void*>(0xbeefdead);

class MockAllocator : public Allocator {
 public:
  MOCK_METHOD(void, DestroyAllocator, ());
  ~MockAllocator() override { DestroyAllocator(); }
  MOCK_METHOD(std::unique_ptr<Allocator::BufferObject>,
              CreateBo,
              (int width, int height, uint32_t drm_format, uint32_t gbm_flags),
              (override));
  MOCK_METHOD(std::unique_ptr<Allocator::BufferObject>,
              ImportBo,
              (const ImportData& data),
              (override));
  MOCK_METHOD(bool,
              IsFormatSupported,
              (uint32_t drm_format, uint32_t gbm_flags),
              (override));
};

class MockBufferObject : public Allocator::BufferObject {
 public:
  MOCK_METHOD(void, DestroyBo, ());
  ~MockBufferObject() override { DestroyBo(); }
  MOCK_METHOD(BufferDescriptor, Describe, (), (const, override));
  MOCK_METHOD(bool,
              BeginCpuAccess,
              (SyncType sync_type, int plane),
              (override));
  MOCK_METHOD(bool, EndCpuAccess, (SyncType sync_type, int plane), (override));
  MOCK_METHOD(bool, Map, (int plane), (override));
  MOCK_METHOD(void, Unmap, (int plane), (override));
  MOCK_METHOD(int, GetPlaneFd, (int plane), (const, override));
  MOCK_METHOD(void*, GetPlaneAddr, (int plane), (const, override));
  MOCK_METHOD(uint64_t, GetId, (), (const, override));
};

static size_t GetFormatBpp(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_BGR233:
    case DRM_FORMAT_C8:
    case DRM_FORMAT_R8:
    case DRM_FORMAT_RGB332:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
      return 1;

    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_BGR565:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_GR88:
    case DRM_FORMAT_P010:
    case DRM_FORMAT_RG88:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_VYUY:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
      return 2;

    case DRM_FORMAT_BGR888:
    case DRM_FORMAT_RGB888:
      return 3;

    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_AYUV:
    case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX1010102:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XRGB8888:
      return 4;
  }

  LOG(ERROR) << "Unknown format: " << FormatToString(drm_format);
  return 0;
}

class CameraBufferManagerImplTest : public ::testing::Test {
 public:
  CameraBufferManagerImplTest() = default;
  CameraBufferManagerImplTest(const CameraBufferManagerImplTest&) = delete;
  CameraBufferManagerImplTest& operator=(const CameraBufferManagerImplTest&) =
      delete;

  void SetUp() override {
    auto mock_allocator = std::make_unique<MockAllocator>();
    allocator_ = mock_allocator.get();
    cbm_ = std::make_unique<CameraBufferManagerImpl>(std::move(mock_allocator));
  }

  void TearDown() override {
    // Verify that gbm_device is properly tear down.
    EXPECT_CALL(*allocator_, DestroyAllocator()).Times(1);
    cbm_ = nullptr;
    EXPECT_EQ(::testing::Mock::VerifyAndClear(allocator_), true);
  }

  int CreateFakeFd() { return open("/dev/null", 0); }

  std::unique_ptr<camera_buffer_handle_t> CreateBuffer(
      uint32_t buffer_id,
      uint32_t drm_format,
      uint32_t hal_pixel_format,
      uint32_t width,
      uint32_t height) {
    std::unique_ptr<camera_buffer_handle_t> buffer(new camera_buffer_handle_t);
    buffer->fds[0] = CreateFakeFd();
    buffer->magic = kCameraBufferMagic;
    buffer->buffer_id = buffer_id;
    buffer->drm_format = drm_format;
    buffer->hal_pixel_format = hal_pixel_format;
    buffer->width = width;
    buffer->height = height;
    buffer->strides[0] = width * GetFormatBpp(drm_format);
    buffer->offsets[0] = 0;
    switch (drm_format) {
      case DRM_FORMAT_NV12:
      case DRM_FORMAT_NV21:
      case DRM_FORMAT_P010:
        buffer->strides[1] = width * GetFormatBpp(drm_format);
        buffer->offsets[1] = buffer->strides[0] * height;
        break;
      case DRM_FORMAT_YUV420:
      case DRM_FORMAT_YVU420:
        buffer->strides[1] = width * GetFormatBpp(drm_format) / 2;
        buffer->strides[2] = width * GetFormatBpp(drm_format) / 2;
        buffer->offsets[1] = buffer->strides[0] * height;
        buffer->offsets[2] =
            buffer->offsets[1] + (buffer->strides[1] * height / 2);
        break;
      default:
        // Single planar buffer.
        break;
    }
    return buffer;
  }

 protected:
  std::unique_ptr<CameraBufferManagerImpl> cbm_;

  MockAllocator* allocator_;
};

TEST_F(CameraBufferManagerImplTest, AllocateTest) {
  const uint32_t kBufferWidth = 1280, kBufferHeight = 720,
                 usage = GRALLOC_USAGE_FORCE_I420;
  buffer_handle_t buffer_handle;
  uint32_t stride;

  // Allocate the buffer.
  auto mock_bo = std::make_unique<MockBufferObject>();
  auto* bo_ptr = mock_bo.get();
  EXPECT_CALL(*allocator_,
              CreateBo(kBufferWidth, kBufferHeight, DRM_FORMAT_YUV420,
                       GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN))
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_bo))));
  BufferDescriptor desc = {
      .drm_format = DRM_FORMAT_YUV420,
      .width = kBufferWidth,
      .height = kBufferHeight,
      .gbm_flags = GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN,
      .num_planes = 3,
      .format_modifier = DRM_FORMAT_MOD_INVALID,
      .planes = {
          {.offset = 0, .row_stride = 0},
          {.offset = 0, .row_stride = 0},
          {.offset = 0, .row_stride = 0},
      }};
  EXPECT_CALL(*bo_ptr, Describe()).Times(1).WillOnce(Return(desc));
  for (size_t plane = 0; plane < 3; ++plane) {
    EXPECT_CALL(*bo_ptr, GetPlaneFd(plane))
        .Times(1)
        .WillOnce(Return(CreateFakeFd()));
  }
  EXPECT_EQ(cbm_->Allocate(kBufferWidth, kBufferHeight,
                           HAL_PIXEL_FORMAT_YCbCr_420_888, usage,
                           &buffer_handle, &stride),
            0);

  // Lock the buffer.  All the planes should be mapped.
  for (size_t plane = 0; plane < 3; ++plane) {
    EXPECT_CALL(*bo_ptr, BeginCpuAccess(A<SyncType>(), plane))
        .WillOnce(Return(true));
    EXPECT_CALL(*bo_ptr, Map(plane)).WillOnce(Return(true));
    EXPECT_CALL(*bo_ptr, GetPlaneAddr(plane))
        .Times(1)
        .WillOnce((Return(fake_addr)));
  }
  struct android_ycbcr ycbcr;
  EXPECT_EQ(cbm_->LockYCbCr(buffer_handle, 0, 0, 0, kBufferWidth, kBufferHeight,
                            &ycbcr),
            0);

  // Unlock the buffer.  All the planes should be unmapped.
  for (size_t plane = 0; plane < 3; ++plane) {
    EXPECT_CALL(*bo_ptr, Unmap(plane));
    EXPECT_CALL(*bo_ptr, EndCpuAccess(A<SyncType>(), plane))
        .WillOnce(Return(true));
  }
  EXPECT_EQ(cbm_->Unlock(buffer_handle), 0);

  // Free the buffer.
  EXPECT_CALL(*bo_ptr, DestroyBo()).Times(1);
  EXPECT_EQ(cbm_->Free(buffer_handle), 0);
}

TEST_F(CameraBufferManagerImplTest, LockTest) {
  // Create a dummy buffer.
  const int kBufferWidth = 1280, kBufferHeight = 720;
  auto buffer = CreateBuffer(1, DRM_FORMAT_XBGR8888,
                             HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                             kBufferWidth, kBufferHeight);
  buffer_handle_t handle = reinterpret_cast<buffer_handle_t>(buffer.get());

  // Register the buffer.
  auto mock_bo = std::make_unique<MockBufferObject>();
  auto* bo_ptr = mock_bo.get();
  EXPECT_CALL(*allocator_, ImportBo(A<const ImportData&>()))
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_bo))));
  EXPECT_EQ(cbm_->Register(handle), 0);

  // The call to Lock |handle| should succeed with valid width and height.
  EXPECT_CALL(*bo_ptr, BeginCpuAccess(A<SyncType>(), 0))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*bo_ptr, Map(0)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(*bo_ptr, GetPlaneAddr(0)).Times(1).WillOnce((Return(fake_addr)));
  void* addr;
  EXPECT_EQ(cbm_->Lock(handle, 0, 0, 0, kBufferWidth, kBufferHeight, &addr), 0);
  EXPECT_EQ(addr, fake_addr);

  // And the call to Unlock on |handle| should also succeed.
  EXPECT_CALL(*bo_ptr, Unmap(0)).Times(1);
  EXPECT_CALL(*bo_ptr, EndCpuAccess(A<SyncType>(), 0))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_EQ(cbm_->Unlock(handle), 0);

  // Finally the bo for |handle| should be unmapped and destroyed when we
  // deregister the buffer.
  EXPECT_CALL(*bo_ptr, DestroyBo()).Times(1);
  EXPECT_EQ(cbm_->Deregister(handle), 0);
}

TEST_F(CameraBufferManagerImplTest, LockYCbCrTest) {
  constexpr int kBufferWidth = 1280, kBufferHeight = 720;
  {
    // Create a dummy buffer.
    auto buffer =
        CreateBuffer(1, DRM_FORMAT_YUV420, HAL_PIXEL_FORMAT_YCbCr_420_888,
                     kBufferWidth, kBufferHeight);
    buffer_handle_t handle = reinterpret_cast<buffer_handle_t>(buffer.get());

    // Register the buffer.
    auto mock_bo = std::make_unique<MockBufferObject>();
    auto* bo_ptr = mock_bo.get();
    EXPECT_CALL(*allocator_, ImportBo(A<const ImportData&>()))
        .Times(1)
        .WillOnce(Return(ByMove(std::move(mock_bo))));
    EXPECT_EQ(cbm_->Register(handle), 0);

    // The call to Lock |handle| should succeed with valid width and height.
    for (size_t i = 0; i < 3; ++i) {
      EXPECT_CALL(*bo_ptr, BeginCpuAccess(A<SyncType>(), i))
          .Times(1)
          .WillOnce(Return(true));
      EXPECT_CALL(*bo_ptr, Map(i)).Times(1).WillOnce(Return(true));
      EXPECT_CALL(*bo_ptr, GetPlaneAddr(i))
          .Times(1)
          .WillOnce((Return(reinterpret_cast<uint8_t*>(fake_addr) +
                            buffer->offsets[i])));
    }
    struct android_ycbcr ycbcr;
    EXPECT_EQ(
        cbm_->LockYCbCr(handle, 0, 0, 0, kBufferWidth, kBufferHeight, &ycbcr),
        0);
    EXPECT_EQ(ycbcr.y, fake_addr);
    EXPECT_EQ(ycbcr.cb,
              reinterpret_cast<uint8_t*>(fake_addr) + buffer->offsets[1]);
    EXPECT_EQ(ycbcr.cr,
              reinterpret_cast<uint8_t*>(fake_addr) + buffer->offsets[2]);
    EXPECT_EQ(ycbcr.ystride, buffer->strides[0]);
    EXPECT_EQ(ycbcr.cstride, buffer->strides[1]);
    EXPECT_EQ(ycbcr.chroma_step, 1);

    // And the call to Unlock on |handle| should also succeed.
    for (size_t i = 0; i < 3; ++i) {
      EXPECT_CALL(*bo_ptr, Unmap(i)).Times(1);
      EXPECT_CALL(*bo_ptr, EndCpuAccess(A<SyncType>(), i))
          .Times(1)
          .WillOnce(Return(true));
    }
    EXPECT_EQ(cbm_->Unlock(handle), 0);

    // Finally the bo for |handle| should be unmapped and destroyed when we
    // deregister the buffer.
    EXPECT_CALL(*bo_ptr, DestroyBo()).Times(1);
    EXPECT_EQ(cbm_->Deregister(handle), 0);
  }

  // Test semi-planar buffers with a list of (DRM_format, chroma_step).
  std::vector<std::tuple<uint32_t, size_t>> formats_to_test = {
      {DRM_FORMAT_NV12, 2}, {DRM_FORMAT_P010, 4}};
  for (const auto& f : formats_to_test) {
    auto buffer =
        CreateBuffer(2, std::get<0>(f), HAL_PIXEL_FORMAT_YCbCr_420_888,
                     kBufferWidth, kBufferHeight);
    buffer_handle_t handle = reinterpret_cast<buffer_handle_t>(buffer.get());

    auto mock_bo = std::make_unique<MockBufferObject>();
    auto* bo_ptr = mock_bo.get();
    EXPECT_CALL(*allocator_, ImportBo(A<const ImportData&>()))
        .Times(1)
        .WillOnce(Return(ByMove(std::move(mock_bo))));
    EXPECT_EQ(cbm_->Register(handle), 0);

    for (size_t i = 0; i < 2; ++i) {
      EXPECT_CALL(*bo_ptr, BeginCpuAccess(A<SyncType>(), i))
          .Times(1)
          .WillOnce(Return(true));
      EXPECT_CALL(*bo_ptr, Map(i)).Times(1).WillOnce(Return(true));
      EXPECT_CALL(*bo_ptr, GetPlaneAddr(i))
          .Times(1)
          .WillOnce((Return(reinterpret_cast<uint8_t*>(fake_addr) +
                            buffer->offsets[i])));
    }
    struct android_ycbcr ycbcr;
    EXPECT_EQ(
        cbm_->LockYCbCr(handle, 0, 0, 0, kBufferWidth, kBufferHeight, &ycbcr),
        0);
    EXPECT_EQ(ycbcr.y, fake_addr);
    EXPECT_EQ(ycbcr.cb,
              reinterpret_cast<uint8_t*>(fake_addr) + buffer->offsets[1]);
    EXPECT_EQ(ycbcr.cr, reinterpret_cast<uint8_t*>(fake_addr) +
                            buffer->offsets[1] + (std::get<1>(f) / 2));
    EXPECT_EQ(ycbcr.ystride, buffer->strides[0]);
    EXPECT_EQ(ycbcr.cstride, buffer->strides[1]);
    EXPECT_EQ(ycbcr.chroma_step, std::get<1>(f));

    for (size_t i = 0; i < 2; ++i) {
      EXPECT_CALL(*bo_ptr, Unmap(i)).Times(1);
      EXPECT_CALL(*bo_ptr, EndCpuAccess(A<SyncType>(), i))
          .Times(1)
          .WillOnce(Return(true));
    }
    EXPECT_EQ(cbm_->Unlock(handle), 0);

    EXPECT_CALL(*bo_ptr, DestroyBo()).Times(1);
    EXPECT_EQ(cbm_->Deregister(handle), 0);
  }
}

TEST_F(CameraBufferManagerImplTest, GetPlaneSizeTest) {
  const int kBufferWidth = 1280, kBufferHeight = 720;

  auto gralloc_buffer = CreateBuffer(0, DRM_FORMAT_XBGR8888,
                                     HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                                     kBufferWidth, kBufferHeight);
  buffer_handle_t rgbx_handle =
      reinterpret_cast<buffer_handle_t>(gralloc_buffer.get());
  const size_t kRGBXBufferSize =
      kBufferWidth * kBufferHeight * GetFormatBpp(DRM_FORMAT_XBGR8888);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(rgbx_handle, 0),
            kRGBXBufferSize);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(rgbx_handle, 1), 0);

  auto nv12_buffer =
      CreateBuffer(1, DRM_FORMAT_NV21, HAL_PIXEL_FORMAT_YCbCr_420_888,
                   kBufferWidth, kBufferHeight);
  const size_t kNV12Plane0Size =
      kBufferWidth * kBufferHeight * GetFormatBpp(DRM_FORMAT_NV12);
  const size_t kNV12Plane1Size =
      kBufferWidth * kBufferHeight * GetFormatBpp(DRM_FORMAT_NV12) / 2;
  buffer_handle_t nv12_handle =
      reinterpret_cast<buffer_handle_t>(nv12_buffer.get());
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(nv12_handle, 0),
            kNV12Plane0Size);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(nv12_handle, 1),
            kNV12Plane1Size);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(nv12_handle, 2), 0);

  auto yuv420_buffer =
      CreateBuffer(2, DRM_FORMAT_YUV420, HAL_PIXEL_FORMAT_YCbCr_420_888,
                   kBufferWidth, kBufferHeight);
  const size_t kYuv420Plane0Size =
      kBufferWidth * kBufferHeight * GetFormatBpp(DRM_FORMAT_YUV420);
  const size_t kYuv420Plane12Size =
      kBufferWidth * kBufferHeight * GetFormatBpp(DRM_FORMAT_YUV420) / 4;
  buffer_handle_t yuv420_handle =
      reinterpret_cast<buffer_handle_t>(yuv420_buffer.get());
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(yuv420_handle, 0),
            kYuv420Plane0Size);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(yuv420_handle, 1),
            kYuv420Plane12Size);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(yuv420_handle, 2),
            kYuv420Plane12Size);
  EXPECT_EQ(CameraBufferManagerImpl::GetPlaneSize(yuv420_handle, 3), 0);
}

TEST_F(CameraBufferManagerImplTest, IsValidBufferTest) {
  const int kBufferWidth = 1280, kBufferHeight = 720;
  EXPECT_FALSE(CameraBufferManagerImpl::IsValidBuffer(nullptr));
  auto cbh = CreateBuffer(2, DRM_FORMAT_NV12, HAL_PIXEL_FORMAT_YCbCr_420_888,
                          kBufferWidth, kBufferHeight);
  buffer_handle_t handle = reinterpret_cast<buffer_handle_t>(cbh.get());
  EXPECT_TRUE(CameraBufferManagerImpl::IsValidBuffer(handle));

  cbh->magic = ~cbh->magic;
  EXPECT_FALSE(CameraBufferManagerImpl::IsValidBuffer(handle));
}

TEST_F(CameraBufferManagerImplTest, DeregisterTest) {
  // Create two dummy buffers.
  const int kBufferWidth = 1280, kBufferHeight = 720;
  auto buffer1 =
      CreateBuffer(1, DRM_FORMAT_YUV420, HAL_PIXEL_FORMAT_YCbCr_420_888,
                   kBufferWidth, kBufferHeight);
  buffer_handle_t handle1 = reinterpret_cast<buffer_handle_t>(buffer1.get());
  auto buffer2 =
      CreateBuffer(1, DRM_FORMAT_YUV420, HAL_PIXEL_FORMAT_YCbCr_420_888,
                   kBufferWidth, kBufferHeight);
  buffer_handle_t handle2 = reinterpret_cast<buffer_handle_t>(buffer2.get());

  // Register the buffers.
  auto mock_bo1 = std::make_unique<MockBufferObject>();
  auto* bo1_ptr = mock_bo1.get();
  EXPECT_CALL(*allocator_, ImportBo(A<const ImportData&>()))
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_bo1))));
  EXPECT_EQ(cbm_->Register(handle1), 0);
  auto mock_bo2 = std::make_unique<MockBufferObject>();
  auto* bo2_ptr = mock_bo2.get();
  EXPECT_CALL(*allocator_, ImportBo(A<const ImportData&>()))
      .Times(1)
      .WillOnce(Return(ByMove(std::move(mock_bo2))));
  EXPECT_EQ(cbm_->Register(handle2), 0);

  // Lock both buffers
  struct android_ycbcr ycbcr;
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_CALL(*bo1_ptr, BeginCpuAccess(A<SyncType>(), i))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*bo1_ptr, Map(i)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*bo1_ptr, GetPlaneAddr(i))
        .Times(1)
        .WillOnce((Return(reinterpret_cast<uint8_t*>(fake_addr) +
                          buffer1->offsets[i])));
  }
  EXPECT_EQ(
      cbm_->LockYCbCr(handle1, 0, 0, 0, kBufferWidth, kBufferHeight, &ycbcr),
      0);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_CALL(*bo2_ptr, BeginCpuAccess(A<SyncType>(), i))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*bo2_ptr, Map(i)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*bo2_ptr, GetPlaneAddr(i))
        .Times(1)
        .WillOnce((Return(reinterpret_cast<uint8_t*>(fake_addr) +
                          buffer2->offsets[i])));
  }
  EXPECT_EQ(
      cbm_->LockYCbCr(handle2, 0, 0, 0, kBufferWidth, kBufferHeight, &ycbcr),
      0);

  EXPECT_CALL(*bo1_ptr, DestroyBo()).Times(1);
  EXPECT_EQ(cbm_->Deregister(handle1), 0);

  EXPECT_CALL(*bo2_ptr, DestroyBo()).Times(1);
  EXPECT_EQ(cbm_->Deregister(handle2), 0);
}

}  // namespace cros::tests

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
