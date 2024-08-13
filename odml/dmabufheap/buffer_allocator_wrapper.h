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
 * aosp/system/memory/libdmabufheap/include/BufferAllocator/
 * BufferAllocatorWrapper.h
 */

#ifndef ODML_DMABUFHEAP_BUFFER_ALLOCATOR_WRAPPER_H_
#define ODML_DMABUFHEAP_BUFFER_ALLOCATOR_WRAPPER_H_

#include <stddef.h>

#include "odml/dmabufheap/buffer_allocator.h"
#include "odml/odml_export.h"

// This file is ported from the aosp project.

#ifdef __cplusplus
extern "C" {
typedef class BufferAllocator BufferAllocator;
#else
typedef struct BufferAllocator BufferAllocator;
#endif

ODML_EXPORT BufferAllocator* CreateDmabufHeapBufferAllocator();

ODML_EXPORT void FreeDmabufHeapBufferAllocator(
    BufferAllocator* buffer_allocator);

ODML_EXPORT int DmabufHeapAlloc(BufferAllocator* buffer_allocator,
                                const char* heap_name,
                                size_t len,
                                unsigned int heap_flags,
                                size_t legacy_align);

#ifdef __cplusplus
}
#endif

#endif  // ODML_DMABUFHEAP_BUFFER_ALLOCATOR_WRAPPER_H_
