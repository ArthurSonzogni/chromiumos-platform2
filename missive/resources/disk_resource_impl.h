// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_RESOURCES_DISK_RESOURCE_IMPL_H_
#define MISSIVE_RESOURCES_DISK_RESOURCE_IMPL_H_

#include <atomic>
#include <cstdint>

#include "missive/resources/resource_interface.h"

namespace reporting {

// Interface to resources management by Storage module.
// Must be implemented by the caller base on the platform limitations.
// All APIs are non-blocking.
class DiskResourceImpl : public ResourceInterface {
 public:
  explicit DiskResourceImpl(uint64_t total_size);

  // Implementation of ResourceInterface methods.
  bool Reserve(uint64_t size) override;
  void Discard(uint64_t size) override;
  uint64_t GetTotal() const override;
  uint64_t GetUsed() const override;
  void Test_SetTotal(uint64_t test_total) override;

 private:
  ~DiskResourceImpl() override;

  uint64_t total_;
  std::atomic<uint64_t> used_{0};
};

}  // namespace reporting

#endif  // MISSIVE_RESOURCES_DISK_RESOURCE_IMPL_H_
