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
 * Migrated from
 * aosp/system/memory/libdmabufheap/include/BufferAllocator/BufferAllocator.h
 */

#ifndef ODML_DMABUFHEAP_BUFFER_ALLOCATOR_H_
#define ODML_DMABUFHEAP_BUFFER_ALLOCATOR_H_

#include <string>

class BufferAllocator {
 public:
  BufferAllocator() = default;
  ~BufferAllocator() = default;

  BufferAllocator(const BufferAllocator&) = delete;
  BufferAllocator& operator=(const BufferAllocator&) = delete;

  /* *
   * Returns a dmabuf fd if the allocation in one of the specified heaps is
   * successful and an error code otherwise. If dmabuf heaps are supported,
   * tries to allocate in the specified dmabuf heap. If allocation fails in the
   * specified dmabuf heap and ion_fd is a valid fd, goes through saved heap
   * data to find a heap ID/mask to match the specified heap names and allocates
   * memory as per the specified parameters. For vendor defined heaps with a
   * legacy ION interface(no heap query support), MapNameToIonMask() must be
   * called prior to invocation of Alloc() to map a heap name to an equivalent
   * heap mask and heap flag configuration.
   * @heap_name: name of the heap to allocate in.
   * @len: size of the allocation.
   * @heap_flags: flags passed to heap.
   * @legacy_align: alignment value used only by legacy ION
   */
  int Alloc(const std::string& heap_name,
            size_t len,
            unsigned int heap_flags = 0,
            size_t legacy_align = 0);

 private:
  int DmabufAlloc(const std::string& heap_name, size_t len, int dev_fd);

  std::optional<int> dma_heap_device_fd_;
  std::optional<int> dma_heap_uncached_device_fd_;
};

#endif  // ODML_DMABUFHEAP_BUFFER_ALLOCATOR_H_
