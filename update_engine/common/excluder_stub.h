// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_EXCLUDER_STUB_H_
#define UPDATE_ENGINE_COMMON_EXCLUDER_STUB_H_

#include <string>

#include "update_engine/common/excluder_interface.h"

namespace chromeos_update_engine {

// An implementation of the |ExcluderInterface| that does nothing.
class ExcluderStub : public ExcluderInterface {
 public:
  ExcluderStub() = default;
  ~ExcluderStub() = default;

  // |ExcluderInterface| overrides.
  bool Exclude(const std::string& name) override;
  bool IsExcluded(const std::string& name) override;
  bool Reset() override;

  // Not copyable or movable.
  ExcluderStub(const ExcluderStub&) = delete;
  ExcluderStub& operator=(const ExcluderStub&) = delete;
  ExcluderStub(ExcluderStub&&) = delete;
  ExcluderStub& operator=(ExcluderStub&&) = delete;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_EXCLUDER_STUB_H_
