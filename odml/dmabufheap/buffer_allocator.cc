/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Modified from aosp/system/memory/libdmabufheap/BufferAllocator.cpp:
 * We don't need the ION heap implementations, and also only need to
 * support the 2 heap names that we know exist on ChromeOS.
 */

#include "odml/dmabufheap/buffer_allocator.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <string>

static constexpr char kDmaHeapRoot[] = "/dev/dma_heap/";
// ChromeOS supports only the below 2 heap names. Hardcode them instead of
// trying to detect whether the requested heap exists at runtime.
static constexpr char kSystemHeapName[] = "system";
static constexpr char kSystemUncachedHeapName[] = "system-uncached";

int BufferAllocator::DmabufAlloc(const std::string& heap_name,
                                 size_t len,
                                 int dev_fd) {
  if (dev_fd < 0)
    return dev_fd;

  struct dma_heap_allocation_data heap_data {
    .len = len,  // length of data to be allocated in bytes
    .fd_flags =
        O_RDWR | O_CLOEXEC,  // permissions for the memory to be allocated
  };

  auto ret =
      TEMP_FAILURE_RETRY(ioctl(dev_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data));
  if (ret < 0) {
    return ret;
  }

  return heap_data.fd;
}

int BufferAllocator::Alloc(const std::string& heap_name,
                           size_t len,
                           unsigned int heap_flags,
                           size_t legacy_align) {
  int fd = -1;
  if (heap_name == kSystemHeapName) {
    if (!dma_heap_device_fd_.has_value()) {
      dma_heap_device_fd_ = TEMP_FAILURE_RETRY(
          open((std::string(kDmaHeapRoot) + kSystemHeapName).c_str(),
               O_RDONLY | O_CLOEXEC));
    }
    fd = DmabufAlloc(heap_name, len, *dma_heap_device_fd_);
  } else if (heap_name == kSystemUncachedHeapName) {
    if (!dma_heap_uncached_device_fd_.has_value()) {
      dma_heap_uncached_device_fd_ = TEMP_FAILURE_RETRY(
          open((std::string(kDmaHeapRoot) + kSystemUncachedHeapName).c_str(),
               O_RDONLY | O_CLOEXEC));
    }
    fd = DmabufAlloc(heap_name, len, *dma_heap_uncached_device_fd_);
  }
  return fd;
}
