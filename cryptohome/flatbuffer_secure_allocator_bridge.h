// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FLATBUFFER_SECURE_ALLOCATOR_BRIDGE_H_
#define CRYPTOHOME_FLATBUFFER_SECURE_ALLOCATOR_BRIDGE_H_

#include <brillo/secure_allocator.h>
#include <flatbuffers/flatbuffers.h>

namespace cryptohome {

// This class wraps the SecureAllocator in a flatbuffers::Allocator interface,
// allowing flatbuffers in cryptohome to be put into eraseable memory.
class FlatbufferSecureAllocatorBridge : public flatbuffers::Allocator {
 public:
  uint8_t* allocate(size_t size) override { return allocator_.allocate(size); }

  void deallocate(uint8_t* p, size_t size) override {
    return allocator_.deallocate(p, size);
  }

 private:
  brillo::SecureAllocator<uint8_t> allocator_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FLATBUFFER_SECURE_ALLOCATOR_BRIDGE_H_
