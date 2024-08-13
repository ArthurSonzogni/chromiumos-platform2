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
 * Migrated from aosp/system/memory/libdmabufheap/BufferAllocatorWrapper.cpp
 */

#include "odml/dmabufheap/buffer_allocator_wrapper.h"

#include <stddef.h>

#include <cerrno>

extern "C" {

BufferAllocator* CreateDmabufHeapBufferAllocator() {
  return new BufferAllocator();
}

void FreeDmabufHeapBufferAllocator(BufferAllocator* buffer_allocator) {
  delete buffer_allocator;
}

int DmabufHeapAlloc(BufferAllocator* buffer_allocator,
                    const char* heap_name,
                    size_t len,
                    unsigned int heap_flags,
                    size_t legacy_align) {
  if (!buffer_allocator)
    return -EINVAL;
  return buffer_allocator->Alloc(heap_name, len, heap_flags, legacy_align);
}
}
