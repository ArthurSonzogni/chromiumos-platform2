// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_buffer/allocator.h"

#include <memory>

#include "hardware_buffer/dmabuf_heap_allocator.h"
#include "hardware_buffer/minigbm_allocator.h"

namespace cros {

// static
std::unique_ptr<Allocator> Allocator::Create(Allocator::Backend backend) {
  switch (backend) {
    case Allocator::Backend::kMinigbm:
      return CreateMinigbmAllocator();

    case Allocator::Backend::kDmaBufHeap:
      return CreateDmaBufHeapAllocator();
  }
  return nullptr;
}

}  // namespace cros
