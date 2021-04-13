// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_MEMORY_H_
#define RUNTIME_PROBE_FUNCTIONS_MEMORY_H_

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class MemoryFunction : public PrivilegedProbeFunction {
 public:
  NAME_PROBE_FUNCTION("memory");

  static constexpr auto FromKwargsValue = FromEmptyKwargsValue<MemoryFunction>;

 private:
  DataType EvalImpl() const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_MEMORY_H_
