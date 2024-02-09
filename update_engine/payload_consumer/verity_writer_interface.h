// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_INTERFACE_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_INTERFACE_H_

#include <cstdint>
#include <memory>

#include "update_engine/payload_consumer/install_plan.h"

namespace chromeos_update_engine {

class VerityWriterInterface {
 public:
  VerityWriterInterface(const VerityWriterInterface&) = delete;
  VerityWriterInterface& operator=(const VerityWriterInterface&) = delete;

  virtual ~VerityWriterInterface() = default;

  virtual bool Init(const InstallPlan::Partition& partition) = 0;
  // Update partition data at [offset : offset + size) stored in |buffer|.
  // Data not in |hash_tree_data_extent| or |fec_data_extent| is ignored.
  // Will write verity data to the target partition once all the necessary
  // blocks has passed.
  virtual bool Update(uint64_t offset, const uint8_t* buffer, size_t size) = 0;

 protected:
  VerityWriterInterface() = default;
};

namespace verity_writer {
std::unique_ptr<VerityWriterInterface> CreateVerityWriter();
}

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_INTERFACE_H_
