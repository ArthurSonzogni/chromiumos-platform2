// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/context_mock_impl.h"

namespace runtime_probe {
ContextMockImpl::ContextMockImpl() {
  CHECK(temp_dir_.CreateUniqueTempDir());
  root_dir_ = temp_dir_.GetPath();
}

ContextMockImpl::~ContextMockImpl() = default;

}  // namespace runtime_probe
