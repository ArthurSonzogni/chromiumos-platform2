// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/verity_writer_stub.h"

#include <memory>

namespace chromeos_update_engine {

namespace verity_writer {
std::unique_ptr<VerityWriterInterface> CreateVerityWriter() {
  return std::make_unique<VerityWriterStub>();
}
}  // namespace verity_writer

bool VerityWriterStub::Init(const InstallPlan::Partition& partition) {
  return partition.hash_tree_size == 0 && partition.fec_size == 0;
}

bool VerityWriterStub::Update(uint64_t offset,
                              const uint8_t* buffer,
                              size_t size) {
  return true;
}

}  // namespace chromeos_update_engine
