// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_STUB_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_STUB_H_

#include "update_engine/payload_consumer/verity_writer_interface.h"

namespace chromeos_update_engine {

class VerityWriterStub : public VerityWriterInterface {
 public:
  VerityWriterStub() = default;
  VerityWriterStub(const VerityWriterStub&) = delete;
  VerityWriterStub& operator=(const VerityWriterStub&) = delete;

  ~VerityWriterStub() override = default;

  bool Init(const InstallPlan::Partition& partition) override;
  bool Update(uint64_t offset, const uint8_t* buffer, size_t size) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_VERITY_WRITER_STUB_H_
