// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/system/context_mock_impl.h"

#include <memory>

#include <base/check.h>
#include <libcrossystem/crossystem_fake.h>

namespace hardware_verifier {

ContextMockImpl::ContextMockImpl()
    : fake_crossystem_(std::make_unique<crossystem::fake::CrossystemFake>()) {
  CHECK(temp_dir_.CreateUniqueTempDir());
  root_dir_ = temp_dir_.GetPath();
}

ContextMockImpl::~ContextMockImpl() = default;

}  // namespace hardware_verifier
