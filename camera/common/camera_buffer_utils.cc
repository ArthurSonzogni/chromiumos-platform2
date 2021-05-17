/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/camera_buffer_utils.h"

#include <fstream>
#include <iostream>

#include <base/files/file_util.h>
#include <base/files/memory_mapped_file.h>
#include <drm_fourcc.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

char* GetPlaneAddr(const android_ycbcr& ycbcr,
                   uint32_t drm_format,
                   size_t plane) {
  void* result = nullptr;
  if (plane == 0) {
    result = ycbcr.y;
  } else if (plane == 1) {
    switch (drm_format) {
      case DRM_FORMAT_NV12:
      case DRM_FORMAT_P010:
      case DRM_FORMAT_YUV420:
        result = ycbcr.cb;
        break;

      case DRM_FORMAT_NV21:
      case DRM_FORMAT_YVU420:
        result = ycbcr.cr;
        break;
    }
  } else if (plane == 2) {
    switch (drm_format) {
      case DRM_FORMAT_YUV420:
        result = ycbcr.cr;
        break;

      case DRM_FORMAT_YVU420:
        result = ycbcr.cb;
        break;
    }
  }
  if (result == nullptr) {
    LOGF(ERROR) << "Unsupported DRM pixel format: "
                << FormatToString(drm_format);
  }
  return reinterpret_cast<char*>(result);
}

}  // namespace

bool ReadFileIntoBuffer(buffer_handle_t buffer, base::FilePath file_to_read) {
  if (!base::PathExists(file_to_read)) {
    LOGF(ERROR) << "File " << file_to_read << " does not exist";
    return false;
  }

  std::ifstream input_file;
  input_file.open(file_to_read.value());
  if (!input_file.is_open()) {
    LOGF(ERROR) << "Failed to load from file: " << file_to_read;
    return false;
  }

  size_t num_planes = CameraBufferManager::GetNumPlanes(buffer);
  size_t total_plane_size = 0;
  for (size_t p = 0; p < num_planes; ++p) {
    total_plane_size += CameraBufferManager::GetPlaneSize(buffer, p);
  }
  input_file.seekg(0, input_file.end);
  size_t length = input_file.tellg();
  if (length < total_plane_size) {
    LOGF(ERROR) << "File " << file_to_read
                << " does not have enough data to fill the buffer";
    input_file.close();
    return false;
  }

  CameraBufferManager* buf_mgr = CameraBufferManager::GetInstance();
  size_t width = CameraBufferManager::GetWidth(buffer);
  size_t height = CameraBufferManager::GetHeight(buffer);
  struct android_ycbcr ycbcr = {};
  void* buf_addr = nullptr;
  {
    int ret;
    if (num_planes == 1) {
      ret = buf_mgr->Lock(buffer, 0, 0, 0, width, height, &buf_addr);
    } else {
      ret = buf_mgr->LockYCbCr(buffer, 0, 0, 0, width, height, &ycbcr);
    }
    if (ret != 0) {
      LOGF(ERROR) << "Failed to mmap buffer";
      input_file.close();
      return false;
    }
  }

  size_t offset = 0;
  for (size_t p = 0; p < num_planes; ++p) {
    input_file.seekg(offset, input_file.beg);
    char* dst;
    if (num_planes == 1) {
      dst = reinterpret_cast<char*>(buf_addr);
    } else {
      dst = GetPlaneAddr(ycbcr, CameraBufferManager::GetDrmPixelFormat(buffer),
                         p);
      CHECK(dst);
    }
    size_t plane_size = CameraBufferManager::GetPlaneSize(buffer, p);
    input_file.read(dst, plane_size);
    offset += plane_size;
  }
  input_file.close();

  buf_mgr->Unlock(buffer);

  return true;
}

bool WriteBufferIntoFile(buffer_handle_t buffer, base::FilePath file_to_write) {
  std::ofstream output_file;
  output_file.open(file_to_write.value(), std::ios::binary | std::ios::out);
  if (!output_file.is_open()) {
    LOGF(ERROR) << "Failed to open output file " << file_to_write;
    return false;
  }

  CameraBufferManager* buf_mgr = CameraBufferManager::GetInstance();
  size_t num_planes = CameraBufferManager::GetNumPlanes(buffer);
  size_t width = CameraBufferManager::GetWidth(buffer);
  size_t height = CameraBufferManager::GetHeight(buffer);
  struct android_ycbcr ycbcr = {};
  void* buf_addr = nullptr;
  {
    int ret;
    if (num_planes == 1) {
      ret = buf_mgr->Lock(buffer, 0, 0, 0, width, height, &buf_addr);
    } else {
      ret = buf_mgr->LockYCbCr(buffer, 0, 0, 0, width, height, &ycbcr);
    }
    if (ret != 0) {
      LOGF(ERROR) << "Failed to mmap buffer";
      output_file.close();
      return false;
    }
  }

  for (size_t p = 0; p < num_planes; ++p) {
    char* src;
    if (num_planes == 1) {
      src = reinterpret_cast<char*>(buf_addr);
    } else {
      src = GetPlaneAddr(ycbcr, CameraBufferManager::GetDrmPixelFormat(buffer),
                         p);
    }
    size_t plane_size = CameraBufferManager::GetPlaneSize(buffer, p);
    output_file.write(src, plane_size);
  }
  output_file.close();

  buf_mgr->Unlock(buffer);

  return true;
}

}  // namespace cros
